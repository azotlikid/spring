/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <fstream>
#include "System/mmgr.h"

#include "ExternalAI/EngineOutHandler.h"
#include "CregLoadSaveHandler.h"
#include "Map/ReadMap.h"
#include "Game/Game.h"
#include "Game/GameSetup.h"
#include "Game/GameServer.h"
#include "Game/InMapDrawModel.h"
#include "Game/GlobalUnsynced.h"
#include "Game/WaitCommandsAI.h"
#include "Sim/Features/FeatureHandler.h"
#include "Sim/Units/UnitHandler.h"
#include "Sim/Misc/RadarHandler.h"
#include "Sim/Misc/LosHandler.h"
#include "Sim/Misc/InterceptHandler.h"
#include "Sim/Misc/AirBaseHandler.h"
#include "Sim/Misc/QuadField.h"
#include "Sim/Misc/CategoryHandler.h"
#include "Sim/Misc/TeamHandler.h"
#include "Sim/Misc/Wind.h"
#include "Sim/Projectiles/ProjectileHandler.h"
#include "Sim/Units/CommandAI/BuilderCAI.h"
#include "Sim/Units/Groups/GroupHandler.h"

#include "System/Platform/errorhandler.h"
#include "System/FileSystem/DataDirsAccess.h"
#include "System/FileSystem/FileQueryFlags.h"
#include "System/creg/Serializer.h"
#include "System/Exceptions.h"
#include "System/Log/ILog.h"

CCregLoadSaveHandler::CCregLoadSaveHandler()
	: ifs(NULL)
{}

CCregLoadSaveHandler::~CCregLoadSaveHandler()
{}

class CGameStateCollector
{
	CR_DECLARE(CGameStateCollector);

public:
	CGameStateCollector() {}

	void Serialize(creg::ISerializer& s);

	std::string mapName;
	std::string modName;
};

CR_BIND(CGameStateCollector, );
CR_REG_METADATA(CGameStateCollector, CR_SERIALIZER(Serialize));

static void WriteString(std::ostream& s, std::string& str)
{
	char c;
	// write the string char by char
	for (unsigned int a=0; a < str.length(); a++) {
		c = str[a];
		s.write(&c, sizeof(char));
	}
	// write the string-termination NULL char
	c = '\0';
	s.write(&c, sizeof(char));
}

static void ReadString(std::istream& s, std::string& str)
{
	char c;
	// read until string-termination NULL char is encountered
	s.read(&c, sizeof(char));
	while (c != '\0') {
		str += c;
		s.read(&c, sizeof(char));
	}
}

void CGameStateCollector::Serialize(creg::ISerializer& s)
{
	// GetClass() works because readmap and uh both have to exist already
	s.SerializeObjectInstance(gs, gs->GetClass());
	s.SerializeObjectInstance(gu, gu->GetClass());
	s.SerializeObjectInstance(game, game->GetClass());
	s.SerializeObjectInstance(readmap, readmap->GetClass());
	s.SerializeObjectInstance(qf, qf->GetClass());
	s.SerializeObjectInstance(featureHandler, featureHandler->GetClass());
	s.SerializeObjectInstance(loshandler, loshandler->GetClass());
	s.SerializeObjectInstance(radarhandler, radarhandler->GetClass());
	s.SerializeObjectInstance(airBaseHandler, airBaseHandler->GetClass());
	s.SerializeObjectInstance(&interceptHandler, interceptHandler.GetClass());
	s.SerializeObjectInstance(CCategoryHandler::Instance(), CCategoryHandler::Instance()->GetClass());
	s.SerializeObjectInstance(uh, uh->GetClass());
	s.SerializeObjectInstance(ph, ph->GetClass());
//	std::map<std::string, int> unitRestrictions;
	s.SerializeObjectInstance(&waitCommandsAI, waitCommandsAI.GetClass());
	s.SerializeObjectInstance(&wind, wind.GetClass());
	s.SerializeObjectInstance(inMapDrawerModel, inMapDrawerModel->GetClass());
	for (int a=0; a < teamHandler->ActiveTeams(); a++) {
		s.SerializeObjectInstance(grouphandlers[a], grouphandlers[a]->GetClass());
	}
	s.SerializeObjectInstance(eoh, eoh->GetClass());
//	s.Serialize()
}

static void PrintSize(const char* txt, int size)
{
	if (size > (1024 * 1024 * 1024)) {
		LOG("%s %.1f GB", txt, size / (1024.0f * 1024 * 1024));
	} else if (size >  (1024 * 1024)) {
		LOG("%s %.1f MB", txt, size / (1024.0f * 1024));
	} else if (size > 1024) {
		LOG("%s %.1f KB", txt, size / (1024.0f));
	} else {
		LOG("%s %u B",    txt, size);
	}
}

void CCregLoadSaveHandler::SaveGame(const std::string& file)
{
	LOG("Saving game");
	try {
		std::ofstream ofs(dataDirsAccess.LocateFile(file, FileQueryFlags::WRITE).c_str(), std::ios::out|std::ios::binary);
		if (ofs.bad() || !ofs.is_open()) {
			throw content_error("Unable to save game to file \"" + file + "\"");
		}

		std::string scriptText = gameSetup->gameSetupText;

		WriteString(ofs, scriptText);

		WriteString(ofs, modName);
		WriteString(ofs, mapName);

		CGameStateCollector* gsc = new CGameStateCollector();

		creg::COutputStreamSerializer os;
		os.SavePackage(&ofs, gsc, gsc->GetClass());
		PrintSize("Game",ofs.tellp());
		int aistart = ofs.tellp();
		eoh->Save(&ofs);
		PrintSize("AIs", ((int)ofs.tellp())-aistart);
	} catch (const content_error& ex) {
		LOG_L(L_ERROR, "Save failed(content error): %s", ex.what());
	} catch (const std::exception& ex) {
		LOG_L(L_ERROR, "Save failed: %s", ex.what());
	} catch (const char*& exStr) {
		LOG_L(L_ERROR, "Save failed: %s", exStr);
	} catch (...) {
		LOG_L(L_ERROR, "Save failed(unknown error)");
	}
}

/// this just loads the mapname and some other early stuff
void CCregLoadSaveHandler::LoadGameStartInfo(const std::string& file)
{
	const std::string file2 = FindSaveFile(file);
	ifs = new std::ifstream (dataDirsAccess.LocateFile(file2).c_str(), std::ios::in|std::ios::binary);

	// in case these contained values alredy
	// (this is the case when loading a game through the spring menu eg),
	// we set them to empty strings, as ReadString() does append,
	// and we would end up with the correct value but two times
	// eg: "AbcAbc" instead of "Abc"
	scriptText = "";
	modName = "";
	mapName = "";

	ReadString(*ifs, scriptText);
	if (!scriptText.empty() && !gameSetup) {
		CGameSetup* temp = new CGameSetup();
		if (!temp->Init(scriptText)) {
			delete temp;
			temp = 0;
		} else {
			temp->saveName = file;
			gameSetup = temp;
		}
	}

	ReadString(*ifs, modName);
	ReadString(*ifs, mapName);
}

/// this should be called on frame 0 when the game has started
void CCregLoadSaveHandler::LoadGame()
{
	creg::CInputStreamSerializer inputStream;
	void* pGSC = NULL;
	creg::Class* gsccls = NULL;
	inputStream.LoadPackage(ifs, pGSC, gsccls);

	assert (pGSC && gsccls == CGameStateCollector::StaticClass());

	CGameStateCollector* gsc = (CGameStateCollector*)pGSC;
	delete gsc; // the only job of gsc is to collect gamestate data
	gsc = NULL;
	eoh->Load(ifs);
	delete ifs;
	ifs = NULL;
	//for (int a=0; a < teamHandler->ActiveTeams(); a++) { // For old savegames
	//	if (teamHandler->Team(a)->isDead && eoh->IsSkirmishAI(a)) {
	//		eoh->DestroySkirmishAI(skirmishAIId(a), 2 /* = team died */);
	//	}
	//}
	gs->paused = false;
	if (gameServer) {
		gameServer->isPaused = false;
		gameServer->syncErrorFrame = 0;
	}
}
