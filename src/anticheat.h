#pragma once
#include "globals.h"

extern std::map<int, ACEntityData>         g_acData;
extern std::map<int, std::string>          g_entityIdToUUID;
extern std::map<int, std::string>          g_entityIdToName;
extern std::map<std::string, std::string>  g_uuidToName;

void RunAnticheatChecks(JNIEnv* env, jobject mcObj, jclass mcClass, jclass customGuiClass);
