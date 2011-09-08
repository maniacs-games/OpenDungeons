#ifndef CREATURE_H
#define CREATURE_H

#include <string>
#include <deque>

#include <semaphore.h>
#include <Ogre.h>

class GameMap;
class Creature;
class RoomDojo;
class Weapon;
class Player;
class Field;
namespace CEGUI
{
class Window;
}

#include "CreatureSound.h"
#include "Tile.h"
#include "CreatureClass.h"
#include "AttackableObject.h"
#include "CreatureAction.h"


/*! \brief Position, status, and AI state for a single game creature.
 *
 *  The creature class is the place where an individual creature's state is
 *  stored and manipulated.  The creature class is also used to store creature
 *  class descriptions, since a class decription is really just a subset of the
 *  overall creature information.  This is not really an optimal design and
 *  will probably be refined later but it works fine for now and the code
 *  affected by this change is relatively limited.
 */
class Creature: public CreatureClass, public AttackableObject
{
    public:
        Creature(GameMap* gameMap);
        //~Creature();

        // Individual properties
        Weapon *weaponL, *weaponR; // The weapons the creature is holding
        int color; // The color of the player who controls this creature
        int level;
        double exp;
        Tile::TileClearType tilePassability; //FIXME:  This is not set from file yet.  Also, it should be moved to CreatureClass.
        Tile *homeTile;
        RoomDojo *trainingDojo;
        int trainWait;

        // Object methods
        void createMesh();
        void destroyMesh();
        void deleteYourself();
        std::string getUniqueCreatureName();

        void createStatsWindow();
        void destroyStatsWindow();
        void updateStatsWindow();
        std::string getStatsText();

        void setPosition(Ogre::Real x, Ogre::Real y, Ogre::Real z);
        void setPosition(const Ogre::Vector3& v);

        void setHP(double nHP);
        //FIXME: Why is tile a parameter here? It's not used.
        double getHP(Tile *tile) { return getHP(); };
        double getHP() const;

        bool getIsOnMap() const;
        void setIsOnMap(bool nIsOnMap);

        void setMana(double nMana);
        double getMana() const;

        int     getDeathCounter() const     { return deathCounter; }
        void    setDeathCounter(int nCount) { deathCounter = nCount; }

        double  getMoveSpeed() const        { return moveSpeed; }

        // AI stuff
        virtual void doTurn();
        double getHitroll(double range);
        double getDefense() const;
        void doLevelUp();
        std::vector<Tile*> visibleTiles;
        std::vector<AttackableObject*> visibleEnemyObjects;
        std::vector<AttackableObject*> reachableEnemyObjects;
        std::vector<AttackableObject*> enemyObjectsInRange;
        std::vector<AttackableObject*> livingEnemyObjectsInRange;
        std::vector<AttackableObject*> visibleAlliedObjects;
        std::vector<AttackableObject*> reachableAlliedObjects;
        void updateVisibleTiles();
        std::vector<AttackableObject*> getVisibleEnemyObjects();
        std::vector<AttackableObject*> getReachableAttackableObjects(
                const std::vector<AttackableObject*> &objectsToCheck,
                unsigned int *minRange, AttackableObject **nearestObject);
        std::vector<AttackableObject*> getEnemyObjectsInRange(
                const std::vector<AttackableObject*> &enemyObjectsToCheck);
        std::vector<AttackableObject*> getVisibleAlliedObjects();
        std::vector<Tile*> getVisibleMarkedTiles();
        std::vector<AttackableObject*> getVisibleForce(int color, bool invert);
        Tile* positionTile();
        std::vector<Tile*> getCoveredTiles();
        bool isMobile() const;
        int getLevel() const;
        int getColor() const;
        void setColor(int nColor);
        void takeDamage(double damage, Tile *tileTakingDamage);
        void recieveExp(double experience);
        AttackableObject::AttackableObjectType getAttackableObjectType() const;
        const std::string& getName() const;
        void clearActionQueue();

        Player* getControllingPlayer();
        void computeBattlefield();

        // Visual debugging routines
        void createVisualDebugEntities();
        void destroyVisualDebugEntities();
        bool getHasVisualDebuggingEntities();

        static std::string getFormat();
        friend std::ostream& operator<<(std::ostream& os, Creature *c);
        friend std::istream& operator>>(std::istream& is, Creature *c);
        Creature& operator=(CreatureClass c2);

        static const int maxGoldCarriedByWorkers = 1500;

    private:
        void pushAction(CreatureAction action);
        void popAction();
        CreatureAction peekAction();

        mutable sem_t   hpLockSemaphore;
        mutable sem_t   manaLockSemaphore;
        mutable sem_t   isOnMapLockSemaphore;
        sem_t           actionQueueLockSemaphore;
        sem_t           statsWindowLockSemaphore;

        bool            isOnMap;
        bool            hasVisualDebuggingEntities;
        bool            meshesExist;
        double          awakeness;
        double          hp;
        double          mana;
        int             deathCounter;
        int             gold;
        int             battleFieldAgeCounter;
        Tile*           previousPositionTile;
        Field*          battleField;
        CEGUI::Window*  statsWindow;

        std::deque<CreatureAction>      actionQueue;
        std::list<Tile*>                visualDebugEntityTiles;
        Ogre::SharedPtr<CreatureSound>  sound;
};

#endif
