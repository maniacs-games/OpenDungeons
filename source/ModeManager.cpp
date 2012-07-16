#include "ModeManager.h"
#include "ModeContext.h"
#include "MenuMode.h"
#include "GameMode.h"
#include "EditorMode.h"

ModeManager::ModeManager(GameMap* gameMap,MiniMap* miniMap){
    

    mc = new ModeContext(gameMap,miniMap);
    modesArray[0]= new MenuMode(mc);
    modesArray[1]= new GameMode(mc);
    modesArray[2]= new EditorMode(mc);
    modesStack.push(MENU);
    modesArray[modesStack.top()]->giveFocus();
}



ModeManager::~ModeManager(){

    delete modesArray[0];
    delete modesArray[1];
    delete modesArray[2];
    delete mc;
}


AbstractApplicationMode* ModeManager::getCurrentMode(){

    return modesArray[modesStack.top()] ;


}

AbstractApplicationMode* ModeManager::progressMode(ModeType mm){
    modesStack.push(mm);
    modesArray[modesStack.top()]->giveFocus();
    return modesArray[modesStack.top()];


}



AbstractApplicationMode* ModeManager::regressMode(){
    if( modesStack.size() > 1){
	modesStack.pop();
	modesArray[modesStack.top()]->giveFocus();
	return modesArray[modesStack.top()];
    }
    else 
	return modesArray[modesStack.top()]; 
}
