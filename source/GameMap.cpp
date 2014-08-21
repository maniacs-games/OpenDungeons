/*!
 * \file   GameMap.cpp
 * \brief  The central object holding everything that is on the map
 *
 *  Copyright (C) 2011-2014  OpenDungeons Team
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef _MSC_VER
#define snprintf_is_banned_in_OD_code _snprintf
#endif

#include "GameMap.h"

#include "ODServer.h"
#include "ODFrameListener.h"
#include "ServerNotification.h"
#include "RadialVector2.h"
#include "Tile.h"
#include "Creature.h"
#include "Player.h"
#include "ResourceManager.h"
#include "Trap.h"
#include "Seat.h"
#include "MapLight.h"
#include "TileCoordinateMap.h"
#include "MissileObject.h"
#include "Weapon.h"
#include "MapLoader.h"
#include "LogManager.h"
#include "MortuaryQuad.h"
#include "CullingManager.h"
#include "RoomDungeonTemple.h"
#include "RoomTreasury.h"
#include "Goal.h"

#include <OgreTimer.h>

#include <iostream>
#include <sstream>
#include <algorithm>
#include <string>

#include <cmath>
#include <cstdlib>

using namespace std;

/*! \brief A helper class for the A* search in the GameMap::path function.
*
* This class stores the requisite information about a tile which is placed in
* the search queue for the A-star, or A*, algorithm which is used to
* calculate paths in the path function.
*
* The A* description can be found here:
* http://en.wikipedia.org/wiki/A*_search_algorithm
*/
class AstarEntry
{
public:
    AstarEntry() :
        tile    (NULL),
        parent  (0),
        g       (0),
        h       (0)
    {}

    AstarEntry(Tile* tile, int x1, int y1, int x2, int y2) :
        tile    (tile),
        parent  (0),
        g       (0),
        h       (0)
    {
        setHeuristic(x1, y1, x2, y2);
    }

    void setHeuristic(const int& x1, const int& y1, const int& x2, const int& y2)
    {
        h = fabs(static_cast<double>(x2 - x1)) + fabs(static_cast<double>(y2 - y1));
    }

    inline double fCost() const
    { return g + h; }

    inline Tile* getTile() const
    { return tile; }

    inline void setTile(Tile* newTile)
    { tile = newTile; }

    inline AstarEntry* getParent() const
    { return parent; }

    inline void setParent(AstarEntry* newParent)
    { parent = newParent; }

    inline const double& getG() const
    { return g; }

    inline void setG(const double& newG)
    { g = newG; }

private:
    Tile*       tile;
    AstarEntry* parent;
    double      g;
    double      h;
};

GameMap::GameMap(bool isServerGameMap) :
        mIsServerGameMap(isServerGameMap),
        culm(NULL),
        miscUpkeepTime(0),
        creatureTurnsTime(0),
        mLocalPlayer(NULL),
        mTurnNumber(-1),
        creatureDefinitionFilename("levels/creatures.def"), // default name
        floodFillEnabled(false),
        numCallsTo_path(0),
        tileCoordinateMap(new TileCoordinateMap(100)),
        aiManager(*this),
        mIsPaused(false)
{
    // Init the player
    mLocalPlayer = new Player();
    mLocalPlayer->setNick("defaultNickName");
    mLocalPlayer->setGameMap(this);
    resetUniqueNumbers();
}

GameMap::~GameMap()
{
    clearAll();
    delete tileCoordinateMap;
    delete mLocalPlayer;
}

bool GameMap::LoadLevel(const std::string& levelFilepath)
{
    // Read in the game map filepath
    std::string levelPath = ResourceManager::getSingletonPtr()->getResourcePath()
                            + levelFilepath;

    // TODO The map loader class should be merged back to GameMap.
    MapLoader::readGameMapFromFile(levelPath, *this);
    setLevelFileName(levelFilepath);

    return true;
}

bool GameMap::createNewMap(int sizeX, int sizeY)
{
    stringstream ss;

    if (!allocateMapMemory(sizeX, sizeY))
        return false;

    for (int jj = 0; jj < mMapSizeY; ++jj)
    {
        for (int ii = 0; ii < mMapSizeX; ++ii)
        {
            Tile* tile = new Tile(this, ii, jj);
            tile->setName(Tile::buildName(ii, jj));
            tile->setFullness(tile->getFullness());
            tile->setType(Tile::dirt);
            addTile(tile);
        }
    }

    mTurnNumber = -1;

    return true;
}

void GameMap::setAllFullnessAndNeighbors()
{
    for (int ii = 0; ii < mMapSizeX; ++ii)
    {
        for (int jj = 0; jj < mMapSizeY; ++jj)
        {
            Tile* tile = getTile(ii, jj);
            tile->setFullness(tile->getFullness());
            setTileNeighbors(tile);
        }
    }
}

void GameMap::clearAll()
{
    clearCreatures();
    clearClasses();
    clearTraps();
    clearMissileObjects();

    clearMapLights();
    clearRooms();
    clearTiles();

    clearGoalsForAllSeats();
    clearEmptySeats();
    getLocalPlayer()->setSeat(NULL);
    clearPlayers();
    clearFilledSeats();

    clearAiManager();

    mTurnNumber = -1;
    resetUniqueNumbers();
}

void GameMap::clearCreatures()
{
    for (unsigned int ii = 0; ii < creatures.size(); ++ii)
    {
        removeAnimatedObject(creatures[ii]);
        creatures[ii]->deleteYourself();
    }

    creatures.clear();
}

void GameMap::clearAiManager()
{
   aiManager.clearAIList();
}

void GameMap::clearClasses()
{
    classDescriptions.clear();
}

void GameMap::clearPlayers()
{

    for (unsigned int ii = 0; ii < numPlayers(); ++ii)
    {
        delete players[ii];
    }

    players.clear();
}

void GameMap::resetUniqueNumbers()
{
    mUniqueNumberBattlefield = 0;
    mUniqueNumberCreature = 0;
    mUniqueNumberFloodFilling = 0;
    mUniqueNumberMissileObj = 0;
    mUniqueNumberRoom = 0;
    mUniqueNumberRoomObj = 0;
    mUniqueNumberTrap = 0;
    mUniqueNumberMapLight = 0;
}

void GameMap::addClassDescription(CreatureDefinition *c)
{
    boost::shared_ptr<CreatureDefinition> ptr(c);
    classDescriptions.push_back(ptr);
}

void GameMap::addClassDescription(CreatureDefinition c)
{
    boost::shared_ptr<CreatureDefinition> ptr(new CreatureDefinition(c));
    classDescriptions.push_back(ptr);
}

void GameMap::addCreature(Creature *cc)
{
    creatures.push_back(cc);

    cc->positionTile()->addCreature(cc);
    if(!mIsServerGameMap)
        culm->mMyCullingQuad.insert(cc);

    addAnimatedObject(cc);
    cc->setIsOnMap(true);
}

void GameMap::removeCreature(Creature *c)
{
    // Loop over the creatures looking for creature c
    for (unsigned int i = 0; i < creatures.size(); ++i)
    {
        if (c == creatures[i])
        {
            // Creature found
            // Remove the creature from the tile it's in
            c->positionTile()->removeCreature(c);
            creatures.erase(creatures.begin() + i);
            break;
        }
    }

    removeAnimatedObject(c);
    c->setIsOnMap(false);
}

void GameMap::queueEntityForDeletion(GameEntity *ge)
{
    entitiesToDelete.push_back(ge);
}

void GameMap::queueMapLightForDeletion(MapLight *ml)
{
    mapLightsToDelete.push_back(ml);
}

CreatureDefinition* GameMap::getClassDescription(const std::string& className)
{
    for (unsigned int i = 0; i < classDescriptions.size(); ++i)
    {
        if (classDescriptions[i]->getClassName().compare(className) == 0)
            return classDescriptions[i].get();
    }

    return NULL;
}

unsigned int GameMap::numCreatures() const
{
    return creatures.size();
}

std::vector<Creature*> GameMap::getCreaturesByColor(int color)
{
    std::vector<Creature*> tempVector;

    // Loop over all the creatures in the GameMap and add them to the temp vector if their color matches that of the desired seat.
    for (unsigned int i = 0; i < creatures.size(); ++i)
    {
        if (creatures[i]->getColor() == color)
            tempVector.push_back(creatures[i]);
    }

    return tempVector;
}

void GameMap::clearAnimatedObjects()
{
    animatedObjects.clear();
}

void GameMap::addAnimatedObject(MovableGameEntity *a)
{
    animatedObjects.push_back(a);
}

void GameMap::removeAnimatedObject(MovableGameEntity *a)
{
    // Loop over the animatedObjects looking for animatedObject a
    for (unsigned int i = 0; i < animatedObjects.size(); ++i)
    {
        if (a == animatedObjects[i])
        {
            // AnimatedObject found
            animatedObjects.erase(animatedObjects.begin() + i);
            break;
        }
    }
}

MovableGameEntity* GameMap::getAnimatedObject(int index)
{
    MovableGameEntity* tempAnimatedObject = animatedObjects[index];

    return tempAnimatedObject;
}

MovableGameEntity* GameMap::getAnimatedObject(const std::string& name)
{
    for (unsigned int i = 0; i < animatedObjects.size(); ++i)
    {
        MovableGameEntity* mge = animatedObjects[i];
        if (mge->getName().compare(name) == 0)
        {
            return mge;
        }
    }

    return NULL;
}

unsigned int GameMap::numAnimatedObjects()
{
    return animatedObjects.size();
}

void GameMap::addActiveObject(GameEntity *a)
{
    if (a->isActive())
        activeObjects.push_back(a);
}

void GameMap::removeActiveObject(GameEntity *a)
{
    if (a->isActive())
    {
        // Loop over the activeObjects looking for activeObject a
        for (unsigned int i = 0; i < activeObjects.size(); ++i)
        {
            if (a == activeObjects[i])
            {
                // ActiveObject found
                activeObjects.erase(activeObjects.begin() + i);
                break;
            }
        }
    }
}

unsigned int GameMap::numClassDescriptions()
{
    return classDescriptions.size();
}

Creature* GameMap::getCreature(int index)
{
    Creature *tempCreature = creatures[index];
    return tempCreature;
}

const Creature* GameMap::getCreature(int index) const
{
    const Creature *tempCreature = creatures[index];
    return tempCreature;
}

CreatureDefinition* GameMap::getClassDescription(int index)
{
    return classDescriptions[index].get();
}

void GameMap::createAllEntities()
{
    // Create OGRE entities for map tiles
    for (int jj = 0; jj < getMapSizeY(); ++jj)
    {
        for (int ii = 0; ii < getMapSizeX(); ++ii)
        {
            getTile(ii,jj)->createMesh();
        }
    }

    // Create OGRE entities for the creatures
    for (unsigned int i = 0, num = numCreatures(); i < num; ++i)
    {
        Creature *currentCreature = getCreature(i);
        currentCreature->createMesh();
        currentCreature->getWeaponL()->createMesh();
        currentCreature->getWeaponR()->createMesh();
    }

    // Create OGRE entities for the map lights.
    for (unsigned int i = 0, num = numMapLights(); i < num; ++i)
    {
        getMapLight(i)->createOgreEntity();
    }

    // Create OGRE entities for the rooms
    for (unsigned int i = 0, num = numRooms(); i < num; ++i)
    {
        getRoom(i)->createMesh();
    }

    // Create OGRE entities for the rooms
    for (unsigned int i = 0, num = numTraps(); i < num; ++i)
    {
        getTrap(i)->createMesh();
    }
    LogManager::getSingleton().logMessage("entities created");
}

void GameMap::destroyAllEntities()
{
    // Destroy OGRE entities for map tiles
    for (int jj = 0; jj < getMapSizeY(); ++jj)
    {
        for (int ii = 0; ii < getMapSizeX(); ++ii)
        {
            Tile* tile = getTile(ii,jj);
            tile->destroyMesh();
        }
    }

    // Destroy OGRE entities for the creatures
    for (unsigned int i = 0; i < numCreatures(); ++i)
    {
        Creature *currentCreature = getCreature(i);
        currentCreature->getWeaponL()->destroyMesh();
        currentCreature->getWeaponR()->destroyMesh();
        currentCreature->destroyMesh();
    }

    // Destroy OGRE entities for the map lights.
    for (unsigned int i = 0; i < numMapLights(); ++i)
    {
        MapLight *currentMapLight = getMapLight(i);
        currentMapLight->destroyOgreEntity();
    }

    // Destroy OGRE entities for the rooms
    for (unsigned int i = 0; i < numRooms(); ++i)
    {
        Room *currentRoom = getRoom(i);
        currentRoom->destroyMesh();
    }

    // Destroy OGRE entities for the traps
    for (unsigned int i = 0; i < numTraps(); ++i)
    {
        Trap* trap = getTrap(i);
        trap->destroyMesh();
    }
}

Creature* GameMap::getCreature(const std::string& cName)
{
    //TODO: This function should look the name up in a map of creature names onto pointers, care should also be taken to minimize calls to this function.
    Creature *returnValue = NULL;

    for (unsigned int i = 0; i < creatures.size(); ++i)
    {
        if (creatures[i]->getName().compare(cName) == 0)
        {
            returnValue = creatures[i];
            break;
        }
    }

    return returnValue;
}

const Creature* GameMap::getCreature(const std::string& cName) const
{
    //TODO: This function should look the name up in a map of creature names onto pointers, care should also be taken to minimize calls to this function.
    Creature *returnValue = NULL;

    for (unsigned int i = 0; i < creatures.size(); ++i)
    {
        if (creatures[i]->getName().compare(cName) == 0)
        {
            returnValue = creatures[i];
            break;
        }
    }

    return returnValue;
}

void GameMap::doTurn()
{
    std::cout << "\nComputing turn " << mTurnNumber;
    unsigned int numCallsTo_path_atStart = numCallsTo_path;

    // Creatures turn should occur before miscUpkeep
    creatureTurnsTime = doCreatureTurns();
    miscUpkeepTime = doMiscUpkeep();

    // Remove dead creatures from the map and put them into the deletion queue.
    unsigned int cptCreature = 0;
    while (cptCreature < numCreatures())
    {
        // Check to see if the creature has died.
        Creature *tempCreature = creatures[cptCreature];
        if (tempCreature->getHP() > 0.0)
        {
            // Since the creature is still alive we add it to the controlled creatures.
            Player *tempPlayer = tempCreature->getControllingPlayer();
            if (tempPlayer != NULL)
            {
                Seat *tempSeat = tempPlayer->getSeat();

                ++(tempSeat->mNumCreaturesControlled);
            }
        }

        ++cptCreature;
    }

    std::cout << "\nDuring this turn there were " << numCallsTo_path
              - numCallsTo_path_atStart << " calls to GameMap::path().";
}

void GameMap::doPlayerAITurn(double frameTime)
{
    aiManager.doTurn(frameTime);
}

unsigned long int GameMap::doMiscUpkeep()
{
    Tile *tempTile;
    Seat *tempSeat;
    Ogre::Timer stopwatch;
    unsigned long int timeTaken;

    // Loop over all the filled seats in the game and check all the unfinished goals for each seat.
    // Add any seats with no remaining goals to the winningSeats vector.
    for (unsigned int i = 0; i < numFilledSeats(); ++i)
    {
        // Check the previously completed goals to make sure they are still met.
        filledSeats[i]->checkAllCompletedGoals();

        // Check the goals and move completed ones to the completedGoals list for the seat.
        //NOTE: Once seats are placed on this list, they stay there even if goals are unmet.  We may want to change this.
        if (filledSeats[i]->checkAllGoals() == 0
                && filledSeats[i]->numFailedGoals() == 0)
            addWinningSeat(filledSeats[i]);

        // Set the creatures count to 0. It will be reset by the next count in doTurn()
        filledSeats[i]->mNumCreaturesControlled = 0;
    }

    // Count how many of each color kobold there are.
    std::map<int, int> koboldColorCounts;
    for (unsigned int i = 0; i < numCreatures(); ++i)
    {
        Creature *tempCreature = creatures[i];

        if (tempCreature->getDefinition()->isWorker())
        {
            int color = tempCreature->getColor();
            ++koboldColorCounts[color];
        }
    }

    // Count how many dungeon temples each color controls.
    std::vector<Room*> dungeonTemples = getRoomsByType(Room::dungeonTemple);
    std::map<int, int> dungeonTempleColorCounts;
    for (unsigned int i = 0, size = dungeonTemples.size(); i < size; ++i)
    {
        ++dungeonTempleColorCounts[dungeonTemples[i]->getColor()];
    }

    // Compute how many kobolds each color should have as determined by the number of dungeon temples they control.
    std::map<int, int>::iterator colorItr = dungeonTempleColorCounts.begin();
    std::map<int, int> koboldsNeededPerColor;
    while (colorItr != dungeonTempleColorCounts.end())
    {
        int color = colorItr->first;
        int numDungeonTemples = colorItr->second;
        int numKobolds = koboldColorCounts[color];
        int numKoboldsNeeded = std::max(4 * numDungeonTemples - numKobolds, 0);
        numKoboldsNeeded = std::min(numKoboldsNeeded, numDungeonTemples);
        koboldsNeededPerColor[color] = numKoboldsNeeded;

        ++colorItr;
    }

    // Loop back over all the dungeon temples and for each one decide if it should try to produce a kobold.
    for (unsigned int i = 0; i < dungeonTemples.size(); ++i)
    {
        RoomDungeonTemple *dungeonTemple = static_cast<RoomDungeonTemple*>(dungeonTemples[i]);
        int color = dungeonTemple->getColor();
        if (koboldsNeededPerColor[color] > 0)
        {
            --koboldsNeededPerColor[color];
            dungeonTemple->produceKobold();
        }
    }

    // Carry out the upkeep round of all the active objects in the game.
    unsigned int activeObjectCount = 0;
    while (activeObjectCount < activeObjects.size())
    {
        GameEntity* ge = activeObjects[activeObjectCount];
        if (!ge->doUpkeep())
        {
            activeObjects.erase(activeObjects.begin() + activeObjectCount);
        }
        else
        {
            ++activeObjectCount;
        }
    }

    while (!newActiveObjects.empty()) // we create new active objects queued by active objects, such as cannon balls
    {
        activeObjects.push_back(newActiveObjects.front());
        newActiveObjects.pop();
    }

    // Remove empty rooms from the GameMap.
    //NOTE:  The auto-increment on this loop is canceled by a decrement in the if statement, changes to the loop structure will need to keep this consistent.
    for (unsigned int i = 0; i < numRooms(); ++i)
    {
        Room *tempRoom = getRoom(i);
        //tempRoom->doUpkeep(tempRoom);

        // Check to see if the room now has 0 covered tiles, if it does we can remove it from the map.
        if (tempRoom->numCoveredTiles() == 0)
        {
            removeRoom(tempRoom);
            tempRoom->deleteYourself();
            --i; //NOTE:  This decrement is to cancel out the increment that will happen on the next loop iteration.
        }
    }

    // Carry out the upkeep round for each seat.  This means recomputing how much gold is
    // available in their treasuries, how much mana they gain/lose during this turn, etc.
    for (unsigned int i = 0; i < filledSeats.size(); ++i)
    {
        tempSeat = filledSeats[i];

        // Add the amount of mana this seat accrued this turn.
        //cout << "\nSeat " << i << " has " << tempSeat->numClaimedTiles << " claimed tiles.";
        tempSeat->mManaDelta = 50 + tempSeat->getNumClaimedTiles();
        tempSeat->mMana += tempSeat->mManaDelta;
        if (tempSeat->mMana > 250000)
            tempSeat->mMana = 250000;

        // Update the count on how much gold is available in all of the treasuries claimed by the given seat.
        tempSeat->mGold = getTotalGoldForColor(tempSeat->mColor);
    }

    // Determine the number of tiles claimed by each seat.
    // Begin by setting the number of claimed tiles for each seat to 0.
    for (unsigned int i = 0; i < filledSeats.size(); ++i)
        filledSeats[i]->setNumClaimedTiles(0);

    for (unsigned int i = 0; i < emptySeats.size(); ++i)
        emptySeats[i]->setNumClaimedTiles(0);

    // Now loop over all of the tiles, if the tile is claimed increment the given seats count.
    for (int jj = 0; jj < getMapSizeY(); ++jj)
    {
        for (int ii = 0; ii < getMapSizeX(); ++ii)
        {
            tempTile = getTile(ii,jj);

            // Check to see if the current tile is claimed by anyone.
            if (tempTile->getType() == Tile::claimed)
            {
                // Increment the count of the seat who owns the tile.
                tempSeat = getSeatByColor(tempTile->getColor());
                if (tempSeat != NULL)
                {
                    tempSeat->incrementNumClaimedTiles();
                }
            }


        }
    }

    timeTaken = stopwatch.getMicroseconds();
    return timeTaken;
}

unsigned long int GameMap::doCreatureTurns()
{
    Ogre::Timer stopwatch;

    unsigned int numCreatures = creatures.size();
    for (unsigned int i = 0; i < numCreatures; ++i)
    {
        creatures[i]->doTurn();
    }

    return stopwatch.getMicroseconds();
}

void GameMap::updateAnimations(Ogre::Real timeSinceLastFrame)
{
    // During the first turn, we setup everything
    if(!isServerGameMap() && getTurnNumber() == 0)
    {
        LogManager::getSingleton().logMessage("Starting game map");
        setGamePaused(false);

        // Destroy the meshes associated with the map lights that allow you to see/drag them in the map editor.
        clearMapLightIndicators();

        // Check whether at least a local player was added.
        Seat* localPlayerSeat = getLocalPlayer()->getSeat();
        if (localPlayerSeat == NULL)
        {
            LogManager::getSingleton().logMessage("FATAL ERROR : Can't start the game: No seat set for local player");
            exit(1);
        }

        // Move camera to starting position
        Ogre::Real startX = (Ogre::Real)(localPlayerSeat->mStartingX);
        Ogre::Real startY = (Ogre::Real)(localPlayerSeat->mStartingY);
        // We make the temple appear in the center of the game view
        startY = (Ogre::Real)(startY - 7.0);
        // Bound check
        if (startY <= 0.0)
            startY = 0.0;

        ODFrameListener::getSingleton().cm->setCameraPosition(Ogre::Vector3(startX, startY, MAX_CAMERA_Z));

        // Create ogre entities for the tiles, rooms, and creatures
        createAllEntities();
    }

    if(mIsPaused)
        return;

    // Update the animations on any AnimatedObjects which have them
    unsigned int entities_number = numAnimatedObjects();
    for (unsigned int i = 0; i < entities_number; ++i)
    {
        MovableGameEntity* currentAnimatedObject = getAnimatedObject(i);

        if (!currentAnimatedObject)
            continue;

        currentAnimatedObject->update(timeSinceLastFrame);
    }

    if(isServerGameMap())
        return;

    // Advance the "flickering" of the lights by the amount of time that has passed since the last frame.
    entities_number = numMapLights();
    for (unsigned int i = 0; i < entities_number; ++i)
    {
        MapLight* tempMapLight = getMapLight(i);

        if (!tempMapLight)
            continue;

        tempMapLight->advanceFlicker(timeSinceLastFrame);
    }
}

bool GameMap::pathExists(int x1, int y1, int x2, int y2,
                         Tile::TileClearType passability, int color)
{
    return (passability == Tile::walkableTile)
           ? walkablePathExists(x1, y1, x2, y2)
           : path(x1, y1, x2, y2, passability, color).size() >= 2;
}

std::list<Tile*> GameMap::path(int x1, int y1, int x2, int y2, Tile::TileClearType passability, int color)
{
    ++numCallsTo_path;
    std::list<Tile*> returnList;

    // If the start tile was not found return an empty path
    if (getTile(x1, y1) == NULL)
        return returnList;

    // If flood filling is enabled, we can possibly eliminate this path by checking to see if they two tiles are colored differently.
    if (floodFillEnabled && passability == Tile::walkableTile
            && !walkablePathExists(x1, y1, x2, y2))
        return returnList;

    // If the end tile was not found return an empty path
    Tile* destination = getTile(x2, y2);
    if (destination == NULL)
        return returnList;

    AstarEntry *currentEntry = new AstarEntry(getTile(x1, y1), x1, y1, x2, y2);

    /* TODO:  Make the openList a priority queue sorted by the
     *        cost to improve lookup times on retrieving the next open item.
     */
    std::list<AstarEntry*> openList;
    openList.push_back(currentEntry);

    /* TODO: make this a local variable don't forget to remove the
     *       delete statement at the end of this function.
     */
    AstarEntry* neighbor = new AstarEntry;
    std::list<AstarEntry*> closedList;
    std::list<AstarEntry*>::iterator itr;
    bool pathFound = false;
    while (true)
    {
        // if the openList is empty we failed to find a path
        if (openList.empty())
            break;

        // Get the lowest fScore from the openList and move it to the closed list
        std::list<AstarEntry*>::iterator itr = openList.begin(), smallestAstar =
                                                   openList.begin();
        while (itr != openList.end())
        {
            if ((*itr)->fCost() < (*smallestAstar)->fCost())
                smallestAstar = itr;
            ++itr;
        }

        currentEntry = *smallestAstar;
        openList.erase(smallestAstar);
        closedList.push_back(currentEntry);

        // We found the path, break out of the search loop
        if (currentEntry->getTile() == destination)
        {
            pathFound = true;
            break;
        }

        // Check the tiles surrounding the current square
        std::vector<Tile*> neighbors = currentEntry->getTile()->getAllNeighbors();
        bool processNeighbor;
        for (unsigned int i = 0; i < neighbors.size(); ++i)
        {
            neighbor->setTile(neighbors[i]);

            processNeighbor = true;
            if (neighbor->getTile() == NULL)
                continue;

            //TODO:  This code is duplicated in GameMap::pathIsClear, it should be moved into a function.
            // See if the neighbor tile in question is passable
            switch (passability)
            {
            default:
            case Tile::walkableTile:
                if (neighbor->getTile()->getTilePassability() != Tile::walkableTile)
                {
                    processNeighbor = false; // skip this tile and go on to the next neighbor tile
                }
                break;

            case Tile::flyableTile:
                if (neighbor->getTile()->getTilePassability() != Tile::walkableTile
                    && neighbor->getTile()->getTilePassability() != Tile::flyableTile)
                {
                    processNeighbor = false; // skip this tile and go on to the next neighbor tile
                }
                break;

            case Tile::diggableTile:
                if (neighbor->getTile()->getTilePassability() != Tile::walkableTile
                    && neighbor->getTile()->isDiggable(color) == false)
                {
                    processNeighbor = false;
                }
                break;

            case Tile::impassableTile:
                break;
            }

            if (!processNeighbor)
                continue;

            // See if the neighbor is in the closed list
            bool neighborFound = false;
            std::list<AstarEntry*>::iterator itr = closedList.begin();
            while (itr != closedList.end())
            {
                if (neighbor->getTile() == (*itr)->getTile())
                {
                    neighborFound = true;
                    break;
                }
                else
                {
                    ++itr;
                }
            }

            // Ignore the neighbor if it is on the closed list
            if (neighborFound)
                continue;

            // See if the neighbor is in the open list
            neighborFound = false;
            itr = openList.begin();
            while (itr != openList.end())
            {
                if (neighbor->getTile() == (*itr)->getTile())
                {
                    neighborFound = true;
                    break;
                }
                else
                {
                    ++itr;
                }
            }

            // If the neighbor is not in the open list
            if (!neighborFound)
            {
                // NOTE: This +1 weights all steps the same, diagonal steps
                // should get a greater wieght iis they are included in the future
                neighbor->setG(currentEntry->getG() + 1);

                // Use the manhattan distance for the heuristic
                currentEntry->setHeuristic(x1, y1, neighbor->getTile()->x, neighbor->getTile()->y);
                neighbor->setParent(currentEntry);

                openList.push_back(new AstarEntry(*neighbor));
            }
            else
            {
                // If this path to the given neighbor tile is a shorter path than the
                // one already given, make this the new parent.
                // NOTE: This +1 weights all steps the same, diagonal steps
                // should get a greater wieght iis they are included in the future
                if (currentEntry->getG() + 1 < (*itr)->getG())
                {
                    // NOTE: This +1 weights all steps the same, diagonal steps
                    // should get a greater wieght iis they are included in the future
                    (*itr)->setG(currentEntry->getG() + 1);
                    (*itr)->setParent(currentEntry);
                }
            }
        }
    }

    if (pathFound)
    {
        //Find the destination tile in the closed list
        //TODO:  Optimize this by remembering this from above so this loop does not need to be carried out.
        itr = closedList.begin();
        while (itr != closedList.end())
        {
            if ((*itr)->getTile() == destination)
                break;
            else
                ++itr;
        }

        // Follow the parent chain back the the starting tile
        currentEntry = (*itr);
        do
        {
            if (currentEntry->getTile() != NULL)
            {
                returnList.push_front(currentEntry->getTile());
                currentEntry = currentEntry->getParent();
            }

        } while (currentEntry != NULL);
    }

    // Clean up the memory we allocated by deleting the astarEntries in the open and closed lists
    itr = openList.begin();
    while (itr != openList.end())
    {
        delete *itr;
        ++itr;
    }

    itr = closedList.begin();
    while (itr != closedList.end())
    {
        delete *itr;
        ++itr;
    }

    delete neighbor;

    return returnList;
}

void GameMap::addPlayer(Player* player, Seat* seat)
{
    player->setSeat(seat);
    player->setGameMap(this);
    players.push_back(player);
    LogManager::getSingleton().logMessage("Added player: " + player->getNick());
}

bool GameMap::assignAI(Player& player, const std::string& aiType, const std::string& parameters)
{
    if (aiManager.assignAI(player, aiType, parameters))
    {
        player.setHasAI(true);
        LogManager::getSingleton().logMessage("Assign AI: " + aiType + ", to player: " + player.getNick());
        return true;
    }

    LogManager::getSingleton().logMessage("Couldn't assign AI: " + aiType + ", to player: " + player.getNick());
    return false;
}

Player* GameMap::getPlayer(int index)
{
    return players[index];
}

const Player* GameMap::getPlayer(int index) const
{
    return players[index];
}

Player* GameMap::getPlayer(const std::string& pName)
{
    for (unsigned int i = 0; i < numPlayers(); ++i)
    {
        if (players[i]->getNick().compare(pName) == 0)
        {
            return players[i];
        }
    }

    return NULL;
}

const Player* GameMap::getPlayer(const std::string& pName) const
{
    for (unsigned int i = 0; i < numPlayers(); ++i)
    {
        if (players[i]->getNick().compare(pName) == 0)
        {
            return players[i];
        }
    }

    return NULL;
}

unsigned int GameMap::numPlayers() const
{
    return players.size();
}

Player* GameMap::getPlayerByColor(int color)
{
    if(!mIsServerGameMap && getLocalPlayer()->getSeat()->getColor() == color)
        return getLocalPlayer();

    for (std::vector<Player*>::iterator it = players.begin(); it != players.end(); ++it)
    {
        Player* player = *it;
        if(player->getSeat()->getColor() == color)
            return player;
    }
    return NULL;
}

bool GameMap::walkablePathExists(int x1, int y1, int x2, int y2)
{
    Tile* tempTile1 = getTile(x1, y1);
    if (tempTile1)
    {
        Tile* tempTile2 = getTile(x2, y2);
        return (tempTile2)
               ? (tempTile1->floodFillColor == tempTile2->floodFillColor)
               : false;
    }

    return false;
}

std::list<Tile*> GameMap::lineOfSight(int x0, int y0, int x1, int y1)
{
    std::list<Tile*> path;

    // Calculate the components of the 'manhattan distance'
    int Dx = x1 - x0;
    int Dy = y1 - y0;

    // Determine if the slope of the line is greater than 1
    int steep = (abs(Dy) >= abs(Dx));
    if (steep)
    {
        std::swap(x0, y0);
        std::swap(x1, y1);
        // recompute Dx, Dy after swap
        Dx = x1 - x0;
        Dy = y1 - y0;
    }

    // Determine whether the x component is increasing or decreasing
    int xstep = 1;
    if (Dx < 0)
    {
        xstep = -1;
        Dx = -Dx;
    }

    // Determine whether the y component is increasing or decreasing
    int ystep = 1;
    if (Dy < 0)
    {
        ystep = -1;
        Dy = -Dy;
    }

    // Loop over the pixels on the line and add them to the return list
    int TwoDy = 2 * Dy;
    int TwoDyTwoDx = TwoDy - 2 * Dx; // 2*Dy - 2*Dx
    int E = TwoDy - Dx; //2*Dy - Dx
    int y = y0;
    int xDraw, yDraw;
    for (int x = x0; x != x1; x += xstep)
    {
        // Treat a steep line as if it were actually its inverse
        if (steep)
        {
            xDraw = y;
            yDraw = x;
        }
        else
        {
            xDraw = x;
            yDraw = y;
        }

        // If the tile exists, add it to the path.
        Tile *currentTile = getTile(xDraw, yDraw);
        if (currentTile != NULL)
        {
            path.push_back(currentTile);
        }
        else
        {
            // This should fix a bug where creatures "cut across" null sections of the map if they can see the other side.
            path.clear();
            return path;
        }

        // If the error has accumulated to the next tile, "increment" the y coordinate
        if (E > 0)
        {
            // Also add the tile for this y-value for the next row over so that the line of sight consists of a 4-connected
            // path (i.e. you can traverse the path without ever having to move "diagonal" on the square grid).
            currentTile = getTile(xDraw + 1, y);
            if (currentTile != NULL)
            {
                path.push_back(currentTile);
            }
            else
            {
                // This should fix a bug where creatures "cut across" null sections of the map if they can see the other side.
                path.clear();
                return path;
            }

            // Now increment y to the value it will be for the next x-value.
            E += TwoDyTwoDx; //E += 2*Dy - 2*Dx;
            y = y + ystep;

        }
        else
        {
            E += TwoDy; //E += 2*Dy;
        }
    }

    return path;
}

std::vector<Tile*> GameMap::visibleTiles(Tile *startTile, double sightRadius)
{
    std::vector<Tile*> tempVector;

    if (!startTile->permitsVision())
        return tempVector;

    int startX = startTile->x;
    int startY = startTile->y;
    int sightRadiusSquared = (int)(sightRadius * sightRadius);
    std::list<std::pair<Tile*, double> > tileQueue;

    int tileCounter = 0;

    while (true)
    {
        int rSquared = tileCoordinateMap->getRadiusSquared(tileCounter);
        if (rSquared > sightRadiusSquared)
            break;

        std::pair<int, int> coord = tileCoordinateMap->getCoordinate(tileCounter);

        Tile *tempTile = getTile(startX + coord.first, startY + coord.second);
        double tempTheta = tileCoordinateMap->getCentralTheta(tileCounter);
        if (tempTile != NULL)
            tileQueue.push_back(std::pair<Tile*, double> (tempTile, tempTheta));

        ++tileCounter;
    }

    //TODO: Loop backwards and remove any non-see through tiles until we get to one which permits vision (this cuts down the cost of walks toward the end when an opaque block is found).

    // Now loop over the queue, determining which tiles are visible and push them onto the tempVector which will be returned as the output of the function.
    while (!tileQueue.empty())
    {
        // If the tile lets light though it it is visible and we can remove it from the queue and put it in the return list.
        if ((*tileQueue.begin()).first->permitsVision())
        {
            // The tile is visible.
            tempVector.push_back((*tileQueue.begin()).first);
            tileQueue.erase(tileQueue.begin());
            continue;
        }
        else
        {
            // The tile is does not allow vision to it.  Remove it from the queue and remove any tiles obscured by this one.
            // We add it to the return list as well since this tile is as far as we can see in this direction.  Calculate
            // the radial vectors to the corners of this tile.
            Tile *obstructingTile = (*tileQueue.begin()).first;
            tempVector.push_back(obstructingTile);
            tileQueue.erase(tileQueue.begin());
            RadialVector2 smallAngle, largeAngle, tempAngle;

            // Calculate the obstructing tile's angular size and the direction to it.  We want to check if other tiles
            // are within deltaTheta of the calculated direction.
            double dx = obstructingTile->x - startTile->x;
            double dy = obstructingTile->y - startTile->y;
            double rsq = dx * dx + dy * dy;
            double deltaTheta = 1.5 / sqrt(rsq);
            tempAngle.fromCartesian(dx, dy);
            smallAngle.setTheta(tempAngle.getTheta() - deltaTheta);
            largeAngle.setTheta(tempAngle.getTheta() + deltaTheta);

            // Now that we have identified the boundary lines of the region obscured by this tile, loop through until the end of
            // the tileQueue and remove any tiles which fall inside this obscured region since they are not visible either.
            std::list<std::pair<Tile*, double> >::iterator tileQueueIterator =
                tileQueue.begin();
            while (tileQueueIterator != tileQueue.end())
            {
                tempAngle.setTheta((*tileQueueIterator).second);

                // If the current tile is in the obscured region.
                if (tempAngle.directionIsBetween(smallAngle, largeAngle))
                {
                    // The tile is in the obscured region so remove it from the queue of possibly visible tiles.
                    tileQueueIterator = tileQueue.erase(tileQueueIterator);
                }
                else
                {
                    // The tile is not obscured by the current obscuring tile so leave it in the queue for now.
                    ++tileQueueIterator;
                }
            }
        }
    }

    //TODO:  Add the sector shaped region of the visible region

    return tempVector;
}

std::vector<GameEntity*> GameMap::getVisibleForce(std::vector<Tile*> visibleTiles, int color, bool invert)
{
    //TODO:  This function also needs to list Rooms, Traps, Doors, etc (maybe add GameMap::getAttackableObjectsInCell to do this).
    std::vector<GameEntity*> returnList;

    // Loop over the visible tiles
    for (std::vector<Tile*>::iterator itr = visibleTiles.begin(), end = visibleTiles.end();
            itr != end; ++itr)
    {
        //TODO: Implement Tile::getAttackableObject() to let you list all attackableObjects in the tile in a single list.
        // Loop over the creatures in the given tile
        for (unsigned int i = 0; i < (*itr)->numCreaturesInCell(); ++i)
        {
            Creature *tempCreature = (*itr)->getCreature(i);
            // If it is an enemy
            if (tempCreature != NULL)
            {
                // The invert flag is used to determine whether we want to return a list of those creatures
                // whose color matches the one supplied or is any color but the one supplied.
                if ((invert && tempCreature->getColor() != color) || (!invert
                        && tempCreature->getColor() == color))
                {
                    // Add the current creature
                    returnList.push_back(tempCreature);
                }
            }
        }

        // Check to see if the tile is covered by a Room, if it is then check to see if it should be added to the returnList.
        Room *tempRoom = (*itr)->getCoveringRoom();
        if (tempRoom != NULL)
        {
            // Check to see if the color is appropriate based on the condition of the invert flag.
            if ((invert && tempRoom->getColor() != color) || (!invert
                    && tempRoom->getColor() != color))
            {
                // Check to see if the given room is already in the returnList.
                bool roomFound = false;
                for (unsigned int i = 0; i < returnList.size(); ++i)
                {
                    if (returnList[i] == tempRoom)
                    {
                        roomFound = true;
                        break;
                    }
                }

                // If the room is not in the return list already then add it.
                if (!roomFound)
                    returnList.push_back(tempRoom);
            }
        }
    }

    return returnList;
}

bool GameMap::pathIsClear(std::list<Tile*> path, Tile::TileClearType passability)
{
    if (path.empty())
        return false;

    std::list<Tile*>::iterator itr;

    // Loop over tile in the path and check to see if it is clear
    bool isClear = true;
    for (itr = path.begin(); itr != path.end() && isClear; ++itr)
    {
        //TODO:  This code is duplicated in GameMap::path, it should be moved into a function.
        // See if the path tile in question is passable
        switch (passability)
        {
            // Walking creatures can only move through walkableTile's.
        case Tile::walkableTile:
            isClear = (isClear && ((*itr)->getTilePassability()
                                   == Tile::walkableTile));
            break;

            // Flying creatures can move through walkableTile's or flyableTile's.
        case Tile::flyableTile:
            isClear = (isClear && ((*itr)->getTilePassability()
                                   == Tile::walkableTile || (*itr)->getTilePassability()
                                   == Tile::flyableTile));
            break;

            // No creatures can walk through impassableTile's
        case Tile::impassableTile:
            isClear = false;
            break;

        default:
            std::cerr
                << "\n\nERROR:  Unhandled tile type in GameMap::pathIsClear()\n\n";
            exit(1);
            break;
        }
    }

    return isClear;
}

void GameMap::cutCorners(std::list<Tile*> &path, Tile::TileClearType passability)
{
    // Size must be >= 3 or else t3 and t4 can end up pointing at the same value
    if (path.size() <= 3)
        return;

    std::list<Tile*>::iterator t1 = path.begin();
    std::list<Tile*>::iterator t2 = t1;
    ++t2;
    std::list<Tile*>::iterator t3;
    std::list<Tile*>::iterator t4;
    std::list<Tile*>::iterator secondLast = path.end();
    --secondLast;

    // Loop t1 over all but the last tile in the path
    while (t1 != path.end())
    {
        // Loop t2 from t1 until the end of the path
        t2 = t1;
        ++t2;

        while (t2 != path.end())
        {
            // If we have a clear line of sight to t2, advance to
            // the next tile else break out of the inner loop
            std::list<Tile*> lineOfSightPath = lineOfSight((*t1)->x, (*t1)->y,
                                               (*t2)->x, (*t2)->y);

            if (pathIsClear(lineOfSightPath, passability))
                ++t2;
            else
                break;
        }

        // Delete the tiles 'strictly between' t1 and t2
        t3 = t1;
        ++t3;
        if (t3 != t2)
        {
            t4 = t2;
            --t4;
            if (t3 != t4)
            {
                path.erase(t3, t4);
            }
        }

        t1 = t2;

        secondLast = path.end();
        --secondLast;
    }
}

void GameMap::clearRooms()
{
    for (unsigned int i = 0; i < rooms.size(); ++i)
    {
        Room *tempRoom = getRoom(i);
        removeActiveObject(tempRoom);
        tempRoom->removeAllRoomObject();
        tempRoom->deleteYourself();
    }

    rooms.clear();
}

void GameMap::addRoom(Room *r)
{
    rooms.push_back(r);
    addActiveObject(r);
}

void GameMap::removeRoom(Room *r)
{
    // For now, rooms are removed when absorbed by another room or when they have no more tile
    // In both cases, the client have enough information to do that alone so no need to notify him
    removeActiveObject(r);

    for (unsigned int i = 0; i < rooms.size(); ++i)
    {
        if (r == rooms[i])
        {
            //TODO:  Loop over the tiles and make any whose coveringRoom variable points to this room point to NULL.
            r->removeAllRoomObject();
            rooms.erase(rooms.begin() + i);
            break;
        }
    }
}

Room* GameMap::getRoom(int index)
{
    return rooms[index];
}

unsigned int GameMap::numRooms()
{
    return rooms.size();
}

std::vector<Room*> GameMap::getRoomsByType(Room::RoomType type)
{
    std::vector<Room*> returnList;
    for (unsigned int i = 0; i < rooms.size(); ++i)
    {
        if (rooms[i]->getType() == type)
            returnList.push_back(rooms[i]);
    }

    return returnList;
}

std::vector<Room*> GameMap::getRoomsByTypeAndColor(Room::RoomType type, int color)
{
    std::vector<Room*> returnList;
    for (unsigned int i = 0; i < rooms.size(); ++i)
    {
        if (rooms[i]->getType() == type && rooms[i]->getColor() == color)
            returnList.push_back(rooms[i]);
    }

    return returnList;
}

std::vector<const Room*> GameMap::getRoomsByTypeAndColor(Room::RoomType type, int color) const
{
    std::vector<const Room*> returnList;
    for (unsigned int i = 0; i < rooms.size(); ++i)
    {
        if (rooms[i]->getType() == type && rooms[i]->getColor() == color)
            returnList.push_back(rooms[i]);
    }

    return returnList;
}

unsigned int GameMap::numRoomsByTypeAndColor(Room::RoomType type, int color) const
{
    unsigned int count = 0;;
    std::vector<Room*>::const_iterator it;
    for (it = rooms.begin(); it != rooms.end(); ++it)
    {
        if ((*it)->getType() == type && (*it)->getColor() == color)
            ++count;
    }
    return count;
}

std::vector<Room*> GameMap::getReachableRooms(const std::vector<Room*>& vec,
                                              Tile* startTile,
                                              Tile::TileClearType passability)
{
    std::vector<Room*> returnVector;

    for (unsigned int i = 0; i < vec.size(); ++i)
    {
        Room* room = vec[i];
        Tile* coveredTile = room->getCoveredTile(0);
        if (pathExists(startTile->x, startTile->y,
            coveredTile->x, coveredTile->y,
            passability))
        {
            returnVector.push_back(room);
        }
    }

    return returnVector;
}

Room* GameMap::getRoomByName(const std::string& name)
{
    for (std::vector<Room*>::const_iterator it = rooms.begin(); it != rooms.end(); ++it)
    {
        Room* room = *it;
        if(room->getName().compare(name) == 0)
            return room;
    }

    return NULL;
}

void GameMap::clearTraps()
{
    for (unsigned int i = 0; i < traps.size(); ++i)
    {
        Trap* trap = traps[i];
        removeActiveObject(trap);
        trap->deleteYourself();
    }

    traps.clear();
}

void GameMap::addTrap(Trap *t)
{
    traps.push_back(t);
    addActiveObject(t);
}

void GameMap::removeTrap(Trap *t)
{
    removeActiveObject(t);

    for (std::vector<Trap*>::iterator it = traps.begin(); it != traps.end(); ++it)
    {
        Trap* trap = *it;
        if (trap == t)
        {
            t->deleteYourself();
            traps.erase(it);
            break;
        }
    }
}

Trap* GameMap::getTrap(int index)
{
    return traps[index];
}

unsigned int GameMap::numTraps()
{
    return traps.size();
}

int GameMap::getTotalGoldForColor(int color)
{
    int tempInt = 0;
    std::vector<Room*> treasuriesOwned = getRoomsByTypeAndColor(Room::treasury, color);
    for (unsigned int i = 0; i < treasuriesOwned.size(); ++i)
    {
        tempInt += static_cast<RoomTreasury*>(treasuriesOwned[i])->getTotalGold();
    }

    return tempInt;
}

bool GameMap::withdrawFromTreasuries(int gold, Seat* seat)
{
    // Check to see if there is enough gold available in all of the treasuries owned by the given color.
    if (seat->getGold() < gold)
        return false;

    // Loop over the treasuries withdrawing gold until the full amount has been withdrawn.
    int goldStillNeeded = gold;
    std::vector<Room*> treasuriesOwned = getRoomsByTypeAndColor(Room::treasury, seat->getColor());
    for (unsigned int i = 0; i < treasuriesOwned.size() && goldStillNeeded > 0; ++i)
    {
        goldStillNeeded -= static_cast<RoomTreasury*>(treasuriesOwned[i])->withdrawGold(goldStillNeeded);
    }

    return true;
}

void GameMap::clearMapLights()
{
    for (unsigned int i = 0; i < mapLights.size(); ++i)
    {
        mapLights[i]->deleteYourself();
    }

    mapLights.clear();
}

void GameMap::clearMapLightIndicators()
{
    for (unsigned int i = 0; i < mapLights.size(); ++i)
        mapLights[i]->destroyOgreEntityVisualIndicator();
}

void GameMap::addMapLight(MapLight *m)
{
    mapLights.push_back(m);

    /*
    // Place a message in the queue to inform the clients about the destruction of this MapLight.
    ServerNotification *serverNotification = new ServerNotification;
    serverNotification->type = ServerNotification::addMapLight;
    serverNotification->p = m;

    queueServerNotification(serverNotification);
    */
}

void GameMap::removeMapLight(MapLight *m)
{
    for (unsigned int i = 0; i < mapLights.size(); ++i)
    {
        if (mapLights[i] == m)
        {
            /*
            // Place a message in the queue to inform the clients about the destruction of this MapLight.
            ServerNotification *serverNotification = new ServerNotification(
                ServerNotification::removeMapLight);
            serverNotification->mPacket << m;
            queueServerNotification(serverNotification);
            */

            mapLights.erase(mapLights.begin() + i);
            break;

        }
    }
}

MapLight* GameMap::getMapLight(int index)
{
    return mapLights[index];
}

MapLight* GameMap::getMapLight(const std::string& name)
{
    for (unsigned int i = 0; i < mapLights.size(); ++i)
    {
        if (mapLights[i]->getName() == name)
            return mapLights[i];
    }

    return NULL;
}

unsigned int GameMap::numMapLights()
{
    return mapLights.size();
}

void GameMap::clearEmptySeats()
{
    for (unsigned int i = 0; i < numEmptySeats(); ++i)
        delete emptySeats[i];

    emptySeats.clear();
}

void GameMap::addEmptySeat(Seat *s)
{
    if (s == NULL)
        return;

    emptySeats.push_back(s);

    // Add the goals for all seats to this seat.
    for (unsigned int i = 0; i < numGoalsForAllSeats(); ++i)
        s->addGoal(getGoalForAllSeats(i));
}

Seat* GameMap::getEmptySeat(int index)
{
    return emptySeats[index];
}

const Seat* GameMap::getEmptySeat(int index) const
{
    return emptySeats[index];
}

Seat* GameMap::getEmptySeat(const std::string& faction)
{
    Seat* seat = NULL;
    for (std::vector<Seat*>::iterator it = emptySeats.begin(); it != emptySeats.end(); ++it)
    {
        if((*it)->mFaction == faction)
        {
            seat = *it;
            break;
        }
    }

    return seat;
}

Seat* GameMap::popEmptySeat(int color)
{
    Seat* seat = NULL;
    for (std::vector<Seat*>::iterator it = emptySeats.begin(); it != emptySeats.end(); ++it)
    {
        if((*it)->getColor() == color)
        {
            seat = *it;
            emptySeats.erase(it);
            filledSeats.push_back(seat);
            break;
        }
    }

    return seat;
}

unsigned int GameMap::numEmptySeats() const
{
    return emptySeats.size();
}

void GameMap::clearFilledSeats()
{
    for (unsigned int i = 0; i < numFilledSeats(); ++i)
        delete filledSeats[i];

    filledSeats.clear();
}

void GameMap::addFilledSeat(Seat *s)
{
    if (s == NULL)
        return;

    filledSeats.push_back(s);

    // Add the goals for all seats to this seat.
    for (unsigned int i = 0; i < numGoalsForAllSeats(); ++i)
        s->addGoal(getGoalForAllSeats(i));
}

Seat* GameMap::getFilledSeat(int index)
{
    return filledSeats[index];
}

const Seat* GameMap::getFilledSeat(int index) const
{
    return filledSeats[index];
}

Seat* GameMap::popFilledSeat()
{
    Seat *s = NULL;
    if (!filledSeats.empty())
    {
        s = filledSeats[0];
        filledSeats.erase(filledSeats.begin());
        emptySeats.push_back(s);
    }

    return s;
}

unsigned int GameMap::numFilledSeats() const
{
    return filledSeats.size();
}

Seat* GameMap::getSeatByColor(int color)
{
    for (unsigned int i = 0; i < filledSeats.size(); ++i)
    {
        if (filledSeats[i]->getColor() == color)
            return filledSeats[i];
    }

    for (unsigned int i = 0; i < emptySeats.size(); ++i)
    {
        if (emptySeats[i]->getColor() == color)
            return emptySeats[i];
    }

    return NULL;
}

void GameMap::addWinningSeat(Seat *s)
{
    // Make sure the seat has not already been added.
    for (unsigned int i = 0; i < winningSeats.size(); ++i)
    {
        if (winningSeats[i] == s)
            return;
    }

    winningSeats.push_back(s);
}

Seat* GameMap::getWinningSeat(unsigned int index)
{
    return winningSeats[index];
}

unsigned int GameMap::getNumWinningSeats()
{
    return winningSeats.size();
}

bool GameMap::seatIsAWinner(Seat *s)
{
    bool isAWinner = false;
    for (unsigned int i = 0; i < getNumWinningSeats(); ++i)
    {
        if (getWinningSeat(i) == s)
        {
            isAWinner = true;
            break;
        }
    }

    return isAWinner;
}

void GameMap::addGoalForAllSeats(Goal *g)
{
    goalsForAllSeats.push_back(g);

    // Add the goal to each of the empty seats currently in the game.
    for (unsigned int i = 0, num = numEmptySeats(); i < num; ++i)
        emptySeats[i]->addGoal(g);

    // Add the goal to each of the filled seats currently in the game.
    for (unsigned int i = 0, num = numFilledSeats(); i < num; ++i)
        filledSeats[i]->addGoal(g);
}

Goal* GameMap::getGoalForAllSeats(unsigned int i)
{
    return goalsForAllSeats[i];
}

const Goal* GameMap::getGoalForAllSeats(unsigned int i) const
{
    return goalsForAllSeats[i];
}

unsigned int GameMap::numGoalsForAllSeats() const
{
    return goalsForAllSeats.size();
}

void GameMap::clearGoalsForAllSeats()
{
    goalsForAllSeats.clear();

    // Add the goal to each of the empty seats currently in the game.
    for (unsigned int i = 0; i < numEmptySeats(); ++i)
    {
        emptySeats[i]->clearUncompleteGoals();
        emptySeats[i]->clearCompletedGoals();
    }

    // Add the goal to each of the filled seats currently in the game.
    for (unsigned int i = 0; i < numFilledSeats(); ++i)
    {
        filledSeats[i]->clearUncompleteGoals();
        filledSeats[i]->clearCompletedGoals();
    }
}

void GameMap::clearMissileObjects()
{
    for (unsigned int i = 0; i < missileObjects.size(); ++i)
    {
        MissileObject* mo = missileObjects[i];
        removeActiveObject(mo);

        for (std::vector<MovableGameEntity*>::iterator it = animatedObjects.begin(); it != animatedObjects.end(); ++it)
        {
            MovableGameEntity* ao = *it;
            if (mo == ao)
            {
                animatedObjects.erase(it);
                break;
            }
        }
        mo->deleteYourself();
    }

    missileObjects.clear();
}

void GameMap::addMissileObject(MissileObject *m)
{
    if(isServerGameMap())
    {
        try
        {
            ServerNotification *serverNotification = new ServerNotification(
                ServerNotification::addMissileObject, NULL);
            serverNotification->mPacket << m;
            ODServer::getSingleton().queueServerNotification(serverNotification);
        }
        catch (std::bad_alloc&)
        {
            Ogre::LogManager::getSingleton().logMessage("ERROR: bad alloc in GameMap::addMissileObject", Ogre::LML_CRITICAL);
            exit(1);
        }
    }

    missileObjects.push_back(m);
    newActiveObjects.push(m);
    addAnimatedObject(m);
}

void GameMap::removeMissileObject(MissileObject *m)
{
    if(isServerGameMap())
    {
        try
        {
            ServerNotification *serverNotification = new ServerNotification(
                ServerNotification::removeMissileObject, NULL);
            std::string name = m->getName();
            serverNotification->mPacket << name;
            ODServer::getSingleton().queueServerNotification(serverNotification);
        }
        catch (std::bad_alloc&)
        {
            Ogre::LogManager::getSingleton().logMessage("ERROR: bad alloc in GameMap::removeMissileObject", Ogre::LML_CRITICAL);
            exit(1);
        }
    }

    removeActiveObject(m);

    for (unsigned int i = 0; i < missileObjects.size(); ++i)
    {
        if (m == missileObjects[i])
        {
            //TODO:  Loop over the tiles and make any whose coveringRoom variable points to this room point to NULL.
            missileObjects.erase(missileObjects.begin() + i);
            break;
        }
    }

    removeAnimatedObject(m);
}

MissileObject* GameMap::getMissileObject(int index)
{
    return missileObjects[index];
}

MissileObject* GameMap::getMissileObject(const std::string& name)
{
    for(std::vector<MissileObject*>::iterator it = missileObjects.begin(); it != missileObjects.end(); ++it)
    {
        MissileObject* mo = *it;
        if(mo->getName().compare(name) == 0)
            return mo;
    }
    return NULL;
}

unsigned int GameMap::numMissileObjects()
{
    return missileObjects.size();
}

Ogre::Real GameMap::crowDistance(Tile *t1, Tile *t2)
{
    if (t1 != NULL && t2 != NULL)
        return crowDistance(t1->x, t2->x, t1->y, t2->y);
    else
        return -1.0f;
}

Ogre::Real GameMap::crowDistance(int x1, int x2, int y1, int y2)
{
    return sqrt(pow(static_cast<Ogre::Real>(x2 - x1), 2.0f)
                + pow(static_cast<Ogre::Real>(y2 - y1), 2.0f));
}

unsigned int GameMap::doFloodFill(int startX, int startY, Tile::TileClearType passability, int color)
{
    unsigned int tilesFlooded = 1;

    if (!floodFillEnabled)
        return 0;

    if (color < 0)
    {
        color = nextUniqueNumberFloodFilling();
    }

    // Check to see if we should color the current tile.
    Tile *tempTile = getTile(startX, startY);
    if (tempTile != NULL)
    {
        // If the tile is walkable, color it.
        //FIXME:  This should be improved to use the "passability" parameter.
        if (tempTile->getTilePassability() == Tile::walkableTile)
            tempTile->floodFillColor = color;
        else
            return 0;
    }

    // Get the current tile's neighbors, loop over each of them.
    std::vector<Tile*> neighbors = neighborTiles(startX, startY);
    for (unsigned int i = 0; i < neighbors.size(); ++i)
    {
        if (neighbors[i]->floodFillColor != color)
        {
            tilesFlooded += doFloodFill(neighbors[i]->x, neighbors[i]->y,
                                        passability, color);
        }
    }

    return tilesFlooded;
}

void GameMap::enableFloodFill()
{
    // Carry out a flood fill of the whole level to make sure everything is good.
    // Start by setting the flood fill color for every tile on the map to -1.
    for (int jj = 0; jj < getMapSizeY(); ++jj)
    {
        for (int ii = 0; ii < getMapSizeX(); ++ii)
        {
            getTile(ii,jj)->floodFillColor = -1;
        }

    }

    // Loop over the tiles again, this time flood filling when the flood fill color is -1.
    // This will flood the map enough times to cover the whole map.

    // TODO: The looping construct here has a potential race condition in that the endTile could change between the time
    // when it is initialized and the end of this loop.  If this happens the loop could continue infinitely.
    floodFillEnabled = true;

    for (int jj = 0; jj < getMapSizeY(); ++jj)
    {
        for (int ii = 0; ii < getMapSizeX(); ++ii)
        {
            if (getTile(ii, jj)->floodFillColor == -1)
                doFloodFill(ii , jj);
        }
    }
}

std::list<Tile*> GameMap::path(Creature *c1, Creature *c2, Tile::TileClearType passability, int color)
{
    return path(c1->positionTile()->x, c1->positionTile()->y,
                c2->positionTile()->x, c2->positionTile()->y, passability, color);
}

std::list<Tile*> GameMap::path(Tile *t1, Tile *t2, Tile::TileClearType passability, int color)
{
    return path(t1->x, t1->y, t2->x, t2->y, passability, color);
}

Ogre::Real GameMap::crowDistance(Creature *c1, Creature *c2)
{
    //TODO:  This is sub-optimal, improve it.
    Tile* tempTile1 = c1->positionTile();
    Tile* tempTile2 = c2->positionTile();
    return crowDistance(tempTile1->x, tempTile1->y, tempTile2->x, tempTile2->y);
}

void GameMap::processDeletionQueues()
{
    LogManager::getSingleton().logMessage("Processing deletion queues on turn "
        + Ogre::StringConverter::toString(static_cast<int32_t>(mTurnNumber)));

    while (entitiesToDelete.size() > 0)
    {
        GameEntity* entity = *entitiesToDelete.begin();
        entitiesToDelete.erase(entitiesToDelete.begin());
        delete entity;
    }

    while (mapLightsToDelete.size() > 0)
    {
        MapLight* ml = *mapLightsToDelete.begin();
        mapLightsToDelete.erase(mapLightsToDelete.begin());
        delete ml;
    }
}

void GameMap::refreshBorderingTilesOf(const std::vector<Tile*>& affectedTiles)
{
    // Add the tiles which border the affected region to the affectedTiles vector since they may need to have their meshes changed.
    std::vector<Tile*> borderTiles = tilesBorderedByRegion(affectedTiles);

    borderTiles.insert(borderTiles.end(), affectedTiles.begin(), affectedTiles.end());

    // Loop over all the affected tiles and force them to examine their neighbors.  This allows
    // them to switch to a mesh with fewer polygons if some are hidden by the neighbors, etc.
    for (std::vector<Tile*>::iterator itr = borderTiles.begin(); itr != borderTiles.end() ; ++itr)
        (*itr)->refreshMesh();
}

std::vector<Tile*> GameMap::getDiggableTilesForPlayerInArea(int x1, int y1, int x2, int y2,
    Player* player)
{
    std::vector<Tile*> tiles = rectangularRegion(x1, y1, x2, y2);
    std::vector<Tile*>::iterator it = tiles.begin();
    while (it != tiles.end())
    {
        Tile* tile = *it;
        if (!tile->isDiggable(player->getSeat()->mColor))
        {
            it = tiles.erase(it);
        }
        else
            ++it;
    }
    return tiles;
}

std::vector<Tile*> GameMap::getBuildableTilesForPlayerInArea(int x1, int y1, int x2, int y2,
    Player* player)
{
    std::vector<Tile*> tiles = rectangularRegion(x1, y1, x2, y2);
    std::vector<Tile*>::iterator it = tiles.begin();
    while (it != tiles.end())
    {
        Tile* tile = *it;
        if (!tile->isBuildableUpon())
        {
            it = tiles.erase(it);
        }
        else if (!(tile->getFullness() < 1
                    && tile->getType() == Tile::claimed
                    && tile->colorDouble > 0.99
                    && tile->getColor() == player->getSeat()->mColor))
        {
            it = tiles.erase(it);
        }
        else
            ++it;
    }
    return tiles;
}

void GameMap::markTilesForPlayer(std::vector<Tile*>& tiles, bool isDigSet, Player* player)
{
    for(std::vector<Tile*>::iterator it = tiles.begin(); it != tiles.end(); ++it)
    {
        Tile* tile = *it;
        tile->setMarkedForDigging(isDigSet, player);
    }
    refreshBorderingTilesOf(tiles);
}

void GameMap::buildRoomForPlayer(std::vector<Tile*>& tiles, Room::RoomType roomType, Player* player)
{
    Room* newRoom = Room::createRoom(this, roomType, tiles, player->getSeat()->getColor());
    Room::setupRoom(this, newRoom, player);
    refreshBorderingTilesOf(tiles);
}

void GameMap::buildTrapForPlayer(std::vector<Tile*>& tiles, Trap::TrapType typeTrap, Player* player)
{
    Trap* newTrap = Trap::createTrap(this, typeTrap, tiles, player->getSeat());
    Trap::setupTrap(this, newTrap, player);
    refreshBorderingTilesOf(tiles);
}

std::string GameMap::getGoalsStringForPlayer(Player* player)
{
    bool playerIsAWinner = seatIsAWinner(player->getSeat());
    std::stringstream tempSS("");
    Seat* seat = player->getSeat();
    seat->resetGoalsChanged();
    if (seat->numUncompleteGoals() > 0)
    {
        // Loop over the list of unmet goals for the seat we are sitting in an print them.
        tempSS << "Unfinished Goals:\n---------------------\n";
        for (unsigned int i = 0; i < seat->numUncompleteGoals(); ++i)
        {
            Goal *tempGoal = seat->getUncompleteGoal(i);
            tempSS << tempGoal->getDescription(seat) << "\n";
        }
    }

    if (seat->numCompletedGoals() > 0)
    {
        // Loop over the list of completed goals for the seat we are sitting in an print them.
        tempSS << "\nCompleted Goals:\n---------------------\n";
        for (unsigned int i = 0; i < seat->numCompletedGoals(); ++i)
        {
            Goal *tempGoal = seat->getCompletedGoal(i);
            tempSS << tempGoal->getSuccessMessage(seat) << "\n";
        }
    }

    if (seat->numFailedGoals() > 0)
    {
        // Loop over the list of completed goals for the seat we are sitting in an print them.
        tempSS << "\nFailed Goals: (You cannot complete this level!)\n---------------------\n";
        for (unsigned int i = 0; i < seat->numFailedGoals(); ++i)
        {
            Goal *tempGoal = seat->getFailedGoal(i);
            tempSS << tempGoal->getFailedMessage(seat) << "\n";
        }
    }

    if (playerIsAWinner)
    {
        tempSS << "\nCongratulations, you have completed this level.";
    }

    return tempSS.str();
}
