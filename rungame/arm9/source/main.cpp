#include <nds.h>
#include <nds/arm9/dldi.h>
#include <stdio.h>
#include <fat.h>
#include "fat_ext.h"
#include <sys/stat.h>
#include <limits.h>

#include <string.h>
#include <unistd.h>

#include "nds_loader_arm9.h"

#include "inifile.h"

#include "ndsheaderbanner.h"
#include "perGameSettings.h"
#include "fileCopy.h"
#include "flashcard.h"
#include "common/tonccpy.h"

#include "saveMap.h"
#include "ROMList.h"

#include "sr_data_srllastran.h"		 // For rebooting into the game

const char* settingsinipath = "sd:/_nds/TWiLightMenu/settings.ini";
const char* bootstrapinipath = "sd:/_nds/nds-bootstrap.ini";

std::string dsiWareSrlPath;
std::string dsiWarePubPath;
std::string dsiWarePrvPath;
std::string homebrewArg[2];
std::string ndsPath;
std::string romfolder;
std::string filename;

const char *charUnlaunchBg;
std::string unlaunchBg = "default.gif";
bool removeLauncherPatches = true;

static const char *unlaunchAutoLoadID = "AutoLoadInfo";

static int consoleModel = 0;
/*	0 = Nintendo DSi (Retail)
	1 = Nintendo DSi (Dev/Panda)
	2 = Nintendo 3DS
	3 = New Nintendo 3DS	*/

static std::string romPath[2];

/**
 * Remove trailing slashes from a pathname, if present.
 * @param path Pathname to modify.
 */
void RemoveTrailingSlashes(std::string& path)
{
	while (!path.empty() && path[path.size()-1] == '/') {
		path.resize(path.size()-1);
	}
}

static const std::string slashchar = "/";
static const std::string woodfat = "fat0:/";
static const std::string dstwofat = "fat1:/";

static bool macroMode = false;
static bool wifiLed = false;
static bool slot1Launched = false;
static int launchType[2] = {0};	// 0 = No launch, 1 = SD/Flash card, 2 = SD/Flash card (Direct boot), 3 = DSiWare, 4 = NES, 5 = (S)GB(C), 6 = SMS/GG
static bool useBootstrap = true;
static bool bootstrapFile = false;
static bool homebrewBootstrap = false;
static bool homebrewHasWide = false;
static bool fcSaveOnSd = false;
static bool wideScreen = false;

static bool soundfreq = false;	// false == 32.73 kHz, true == 47.61 kHz

static int gameLanguage = -1;
static int gameRegion = -2;
static bool boostCpu = false;	// false == NTR, true == TWL
static bool boostVram = false;
static bool bstrap_dsiMode = false;

static bool dsiWareBooter = true;
static bool dsiWareToSD = true;

TWL_CODE void LoadSettings(void) {
	// GUI
	CIniFile settingsini( settingsinipath );

	macroMode = settingsini.GetInt("SRLOADER", "MACRO_MODE", macroMode);
	wifiLed = settingsini.GetInt("SRLOADER", "WIFI_LED", 0);
	soundfreq = settingsini.GetInt("SRLOADER", "SOUND_FREQ", 0);
	consoleModel = settingsini.GetInt("SRLOADER", "CONSOLE_MODEL", 0);
	previousUsedDevice = settingsini.GetInt("SRLOADER", "PREVIOUS_USED_DEVICE", previousUsedDevice);
	fcSaveOnSd = settingsini.GetInt("SRLOADER", "FC_SAVE_ON_SD", fcSaveOnSd);
	useBootstrap = settingsini.GetInt("SRLOADER", "USE_BOOTSTRAP", useBootstrap);
	bootstrapFile = settingsini.GetInt("SRLOADER", "BOOTSTRAP_FILE", 0);

    unlaunchBg = settingsini.GetString("SRLOADER", "UNLAUNCH_BG", unlaunchBg);
	charUnlaunchBg = unlaunchBg.c_str();
	removeLauncherPatches = settingsini.GetInt("SRLOADER", "UNLAUNCH_PATCH_REMOVE", removeLauncherPatches);

	// Default nds-bootstrap settings
	gameLanguage = settingsini.GetInt("NDS-BOOTSTRAP", "LANGUAGE", -1);
	gameRegion = settingsini.GetInt("NDS-BOOTSTRAP", "REGION", -2);
	boostCpu = settingsini.GetInt("NDS-BOOTSTRAP", "BOOST_CPU", 0);
	boostVram = settingsini.GetInt("NDS-BOOTSTRAP", "BOOST_VRAM", 0);
	bstrap_dsiMode = settingsini.GetInt("NDS-BOOTSTRAP", "DSI_MODE", 1);

	dsiWareBooter = settingsini.GetInt("SRLOADER", "DSIWARE_BOOTER", dsiWareBooter);
	dsiWareToSD = settingsini.GetInt("SRLOADER", "DSIWARE_TO_SD", dsiWareToSD);

	dsiWareSrlPath = settingsini.GetString("SRLOADER", "DSIWARE_SRL", "");
	dsiWarePubPath = settingsini.GetString("SRLOADER", "DSIWARE_PUB", "");
	dsiWarePrvPath = settingsini.GetString("SRLOADER", "DSIWARE_PRV", "");
	slot1Launched = settingsini.GetInt("SRLOADER", "SLOT1_LAUNCHED", slot1Launched);
	launchType[0] = settingsini.GetInt("SRLOADER", "LAUNCH_TYPE", launchType[0]);
	launchType[1] = settingsini.GetInt("SRLOADER", "SECONDARY_LAUNCH_TYPE", launchType[1]);
	romPath[0] = settingsini.GetString("SRLOADER", "ROM_PATH", romPath[0]);
	romPath[1] = settingsini.GetString("SRLOADER", "SECONDARY_ROM_PATH", romPath[1]);
	homebrewArg[0] = settingsini.GetString("SRLOADER", "HOMEBREW_ARG", "");
	homebrewArg[1] = settingsini.GetString("SRLOADER", "SECONDARY_HOMEBREW_ARG", "");
	homebrewBootstrap = settingsini.GetInt("SRLOADER", "HOMEBREW_BOOTSTRAP", 0);
	homebrewHasWide = settingsini.GetInt("SRLOADER", "HOMEBREW_HAS_WIDE", 0);

	wideScreen = settingsini.GetInt("SRLOADER", "WIDESCREEN", wideScreen);

	// nds-bootstrap
	CIniFile bootstrapini( bootstrapinipath );

	ndsPath = bootstrapini.GetString( "NDS-BOOTSTRAP", "NDS_PATH", "");
}

using namespace std;

//---------------------------------------------------------------------------------
void stop (void) {
//---------------------------------------------------------------------------------
	while (1) {
		swiWaitForVBlank();
	}
}

char filePath[PATH_MAX];

std::string ReplaceAll(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
    }
    return str;
}

bool extention(const std::string& filename, const char* ext) {
	if(strcasecmp(filename.c_str() + filename.size() - strlen(ext), ext)) {
		return false;
	} else {
		return true;
	}
}

// From FontGraphic class
std::u16string utf8to16(std::string_view text) {
	std::u16string out;
	for(uint i=0;i<text.size();) {
		char16_t c;
		if(!(text[i] & 0x80)) {
			c = text[i++];
		} else if((text[i] & 0xE0) == 0xC0) {
			c  = (text[i++] & 0x1F) << 6;
			c |=  text[i++] & 0x3F;
		} else if((text[i] & 0xF0) == 0xE0) {
			c  = (text[i++] & 0x0F) << 12;
			c |= (text[i++] & 0x3F) << 6;
			c |=  text[i++] & 0x3F;
		} else {
			i++; // out of range or something (This only does up to 0xFFFF since it goes to a U16 anyways)
		}
		out += c;
	}
	return out;
}

void unlaunchBootDSiWare(void) {
	std::u16string path(previousUsedDevice ? u"sdmc:/_nds/TWiLightMenu/tempDSiWare.dsi" : utf8to16(dsiWareSrlPath));
	if (path.substr(0, 3) == u"sd:") {
		path = u"sdmc:" + path.substr(3);
	}

	memcpy((u8*)0x02000800, unlaunchAutoLoadID, 12);
	*(u16*)(0x0200080C) = 0x3F0;			// Unlaunch Length for CRC16 (fixed, must be 3F0h)
	*(u16*)(0x0200080E) = 0;			// Unlaunch CRC16 (empty)
	*(u32*)(0x02000810) |= BIT(0);			// Load the title at 2000838h
	*(u32*)(0x02000810) |= BIT(1);			// Use colors 2000814h
	*(u16*)(0x02000814) = 0x7FFF;			// Unlaunch Upper screen BG color (0..7FFFh)
	*(u16*)(0x02000816) = 0x7FFF;			// Unlaunch Lower screen BG color (0..7FFFh)
	memset((u8*)0x02000818, 0, 0x20+0x208+0x1C0);	// Unlaunch Reserved (zero)
	for (uint i = 0; i < std::min(path.length(), 0x103u); i++) {
		((char16_t*)0x02000838)[i] = path[i];		// Unlaunch Device:/Path/Filename.ext (16bit Unicode,end by 0000h)
	}
	while (*(u16*)(0x0200080E) == 0) {	// Keep running, so that CRC16 isn't 0
		*(u16*)(0x0200080E) = swiCRC16(0xFFFF, (void*)0x02000810, 0x3F0);		// Unlaunch CRC16
	}

	fifoSendValue32(FIFO_USER_08, 1);	// Reboot
	for (int i = 0; i < 15; i++) swiWaitForVBlank();
}

std::vector<char*> argarray;

bool twlBgCxiFound = false;

TWL_CODE void wideCheck(bool useWidescreen) {
	if (consoleModel < 2) return;

	bool wideCheatFound = (access("sd:/_nds/nds-bootstrap/wideCheatData.bin", F_OK) == 0 || access("fat:/_nds/nds-bootstrap/wideCheatData.bin", F_OK) == 0);
	if (useWidescreen && wideCheatFound) {
		if (access("sd:/_nds/TWiLightMenu/TwlBg/Widescreen.cxi", F_OK) == 0) {
			// If title previously launched in widescreen, move Widescreen.cxi again, and reboot again
			if (access("sd:/luma/sysmodules/TwlBg.cxi", F_OK) == 0) {
				rename("sd:/luma/sysmodules/TwlBg.cxi", "sd:/_nds/TWiLightMenu/TwlBg/TwlBg.cxi.bak");
			}
			if (rename("sd:/_nds/TWiLightMenu/TwlBg/Widescreen.cxi", "sd:/luma/sysmodules/TwlBg.cxi") == 0) {
				tonccpy((u32*)0x02000300, sr_data_srllastran, 0x020);
				DC_FlushAll();
				fifoSendValue32(FIFO_USER_08, 1);
				stop();
			}
		} else if (twlBgCxiFound) {
			// Revert back to 4:3 for when returning to TWLMenu++
			if (rename("sd:/luma/sysmodules/TwlBg.cxi", "sd:/_nds/TWiLightMenu/TwlBg/Widescreen.cxi") != 0) {
				consoleDemoInit();
				iprintf("Failed to rename TwlBg.cxi\n");
				iprintf("back to Widescreen.cxi\n");
				for (int i = 0; i < 60*3; i++) swiWaitForVBlank();
			}
			if (access("sd:/_nds/TWiLightMenu/TwlBg/TwlBg.cxi.bak", F_OK) == 0) {
				rename("sd:/_nds/TWiLightMenu/TwlBg/TwlBg.cxi.bak", "sd:/luma/sysmodules/TwlBg.cxi");
			}
		}
	}
}

TWL_CODE int lastRunROM() {
	LoadSettings();

	if (consoleModel < 2) {
		*(u8*)(0x023FFD00) = (wifiLed ? 0x13 : 0);		// WiFi On/Off
	}

	if (macroMode) {
		powerOff(PM_BACKLIGHT_TOP);
	}

	if (consoleModel >= 2) {
		twlBgCxiFound = (access("sd:/luma/sysmodules/TwlBg.cxi", F_OK) == 0);
	}

	argarray.push_back(strdup("null"));

	if (launchType[previousUsedDevice] > 3) {
		argarray.push_back(strdup(homebrewArg[previousUsedDevice].c_str()));
	}

	if (!(*(u32*)(0x02000000) & BIT(3))) {
		if (access(romPath[previousUsedDevice].c_str(), F_OK) != 0 || launchType[previousUsedDevice] == 0) {
			return runNdsFile ("/_nds/TWiLightMenu/main.srldr", 0, NULL, true, false, false, true, true, -1);	// Skip to running TWiLight Menu++
		}

		if (slot1Launched) {
			return runNdsFile ("/_nds/TWiLightMenu/slot1launch.srldr", 0, NULL, true, false, false, true, true, -1);
		}
	}

	switch (launchType[previousUsedDevice]) {
		case 1:
			if ((useBootstrap && !homebrewBootstrap) || !previousUsedDevice)
			{
				std::string savepath;

				romfolder = romPath[previousUsedDevice];
				while (!romfolder.empty() && romfolder[romfolder.size()-1] != '/') {
					romfolder.resize(romfolder.size()-1);
				}
				chdir(romfolder.c_str());

				filename = romPath[previousUsedDevice];
				const size_t last_slash_idx = filename.find_last_of("/");
				if (std::string::npos != last_slash_idx)
				{
					filename.erase(0, last_slash_idx + 1);
				}

				loadPerGameSettings(filename);
				bool useNightly = (perGameSettings_bootstrapFile == -1 ? bootstrapFile : perGameSettings_bootstrapFile);
				bool useWidescreen = (perGameSettings_wideScreen == -1 ? wideScreen : perGameSettings_wideScreen);

				wideCheck(useWidescreen);

				if (!homebrewBootstrap) {
					const char *typeToReplace = ".nds";
					if (extention(filename, ".dsi")) {
						typeToReplace = ".dsi";
					} else if (extention(filename, ".ids")) {
						typeToReplace = ".ids";
					} else if (extention(filename, ".srl")) {
						typeToReplace = ".srl";
					} else if (extention(filename, ".app")) {
						typeToReplace = ".app";
					}

					char game_TID[5];

					FILE *f_nds_file = fopen(filename.c_str(), "rb");

					fseek(f_nds_file, offsetof(sNDSHeadertitlecodeonly, gameCode), SEEK_SET);
					fread(game_TID, 1, 4, f_nds_file);
					game_TID[4] = 0;

					fclose(f_nds_file);

					std::string savename = ReplaceAll(filename, typeToReplace, getSavExtension());
					std::string romFolderNoSlash = romfolder;
					RemoveTrailingSlashes(romFolderNoSlash);
					mkdir ("saves", 0777);
					savepath = romFolderNoSlash+"/saves/"+savename;
					if (previousUsedDevice && fcSaveOnSd) {
						savepath = ReplaceAll(savepath, "fat:/", "sd:/");
					}

					if ((getFileSize(savepath.c_str()) == 0) && (strcmp(game_TID, "####") != 0) && (strncmp(game_TID, "NTR", 3) != 0)) {
						u32 savesize = 524288;	// 512KB (default size for most games)

						u32 gameTidHex = 0;
						memcpy(&gameTidHex, &game_TID, 4);

						for (int i = 0; i < (int)sizeof(ROMList)/12; i++) {
							ROMListEntry* curentry = &ROMList[i];
							if (gameTidHex == curentry->GameCode) {
								if (curentry->SaveMemType != 0xFFFFFFFF) savesize = sramlen[curentry->SaveMemType];
								break;
							}
						}

						if (savesize > 0) {
							consoleDemoInit();
							printf("Creating save file...\n");

							FILE *pFile = fopen(savepath.c_str(), "wb");
							if (pFile) {
								fseek(pFile, savesize - 1, SEEK_SET);
								fputc('\0', pFile);
								fclose(pFile);
							}
							printf("Save file created!\n");
						
							for (int i = 0; i < 30; i++) {
								swiWaitForVBlank();
							}
						}
					}
				}

				char ndsToBoot[256];
				sprintf(ndsToBoot, "sd:/_nds/nds-bootstrap-%s%s.nds", homebrewBootstrap ? "hb-" : "", useNightly ? "nightly" : "release");
				if(access(ndsToBoot, F_OK) != 0) {
					sprintf(ndsToBoot, "fat:/_nds/nds-bootstrap-%s%s.nds", homebrewBootstrap ? "hb-" : "", useNightly ? "nightly" : "release");
				}

				argarray.at(0) = (char *)ndsToBoot;
				if (previousUsedDevice || !homebrewBootstrap) {
					CIniFile bootstrapini(bootstrapinipath);
					bootstrapini.SetString("NDS-BOOTSTRAP", "NDS_PATH", romPath[previousUsedDevice]);
					bootstrapini.SetString("NDS-BOOTSTRAP", "SAV_PATH", savepath);
					// bootstrapini.SetString("NDS-BOOTSTRAP", "GUI_LANGUAGE", ms().getGuiLanguageString());
					bootstrapini.SetInt("NDS-BOOTSTRAP", "LANGUAGE", perGameSettings_language == -2 ? gameLanguage : perGameSettings_language);
					bootstrapini.SetInt("NDS-BOOTSTRAP", "DSI_MODE", perGameSettings_dsiMode == -1 ? bstrap_dsiMode : perGameSettings_dsiMode);
					bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_CPU", perGameSettings_boostCpu == -1 ? boostCpu : perGameSettings_boostCpu);
					bootstrapini.SetInt( "NDS-BOOTSTRAP", "BOOST_VRAM", perGameSettings_boostVram == -1 ? boostVram : perGameSettings_boostVram);
					bootstrapini.SaveIniFile(bootstrapinipath);
				}

				return runNdsFile (argarray[0], argarray.size(), (const char **)&argarray[0], (homebrewBootstrap ? false : true), true, false, true, true, -1);
			} else {
				std::string filename = romPath[1];
				const size_t last_slash_idx = filename.find_last_of("/");
				if (std::string::npos != last_slash_idx)
				{
					filename.erase(0, last_slash_idx + 1);
				}

				loadPerGameSettings(filename);
				bool runNds_boostCpu = perGameSettings_boostCpu == -1 ? boostCpu : perGameSettings_boostCpu;
				bool runNds_boostVram = perGameSettings_boostVram == -1 ? boostVram : perGameSettings_boostVram;

				std::string path;
				if ((memcmp(io_dldi_data->friendlyName, "R4(DS) - Revolution for DS", 26) == 0)
				 || (memcmp(io_dldi_data->friendlyName, "R4iDSN", 6) == 0)) {
					CIniFile fcrompathini("fat:/_wfwd/lastsave.ini");
					path = ReplaceAll(romPath[1], "fat:/", woodfat);
					fcrompathini.SetString("Save Info", "lastLoaded", path);
					fcrompathini.SaveIniFile("fat:/_wfwd/lastsave.ini");
					return runNdsFile("fat:/Wfwd.dat", 0, NULL, true, true, true, runNds_boostCpu, runNds_boostVram, -1);
				} else if (memcmp(io_dldi_data->friendlyName, "Acekard AK2", 0xB) == 0) {
					CIniFile fcrompathini("fat:/_afwd/lastsave.ini");
					path = ReplaceAll(romPath[1], "fat:/", woodfat);
					fcrompathini.SetString("Save Info", "lastLoaded", path);
					fcrompathini.SaveIniFile("fat:/_afwd/lastsave.ini");
					return runNdsFile("fat:/Afwd.dat", 0, NULL, true, true, true, runNds_boostCpu, runNds_boostVram, -1);
				} else if (memcmp(io_dldi_data->friendlyName, "DSTWO(Slot-1)", 0xD) == 0) {
					CIniFile fcrompathini("fat:/_dstwo/autoboot.ini");
					path = ReplaceAll(romPath[1], "fat:/", dstwofat);
					fcrompathini.SetString("Dir Info", "fullName", path);
					fcrompathini.SaveIniFile("fat:/_dstwo/autoboot.ini");
					return runNdsFile("fat:/_dstwo/autoboot.nds", 0, NULL, true, true, true, runNds_boostCpu, runNds_boostVram, -1);
				} else if ((memcmp(io_dldi_data->friendlyName, "TTCARD", 6) == 0)
						 || (memcmp(io_dldi_data->friendlyName, "DSTT", 4) == 0)
						 || (memcmp(io_dldi_data->friendlyName, "DEMON", 5) == 0)) {
					CIniFile fcrompathini("fat:/TTMenu/YSMenu.ini");
					path = ReplaceAll(romPath[1], "fat:/", slashchar);
					fcrompathini.SetString("YSMENU", "AUTO_BOOT", path);
					fcrompathini.SaveIniFile("fat:/TTMenu/YSMenu.ini");
					return runNdsFile("fat:/YSMenu.nds", 0, NULL, true, true, true, runNds_boostCpu, runNds_boostVram, -1);
				}
			}
		case 2: {
			romfolder = romPath[previousUsedDevice];
			while (!romfolder.empty() && romfolder[romfolder.size()-1] != '/') {
				romfolder.resize(romfolder.size()-1);
			}
			chdir(romfolder.c_str());

			filename = romPath[previousUsedDevice];
			const size_t last_slash_idx = filename.find_last_of("/");
			if (std::string::npos != last_slash_idx)
			{
				filename.erase(0, last_slash_idx + 1);
			}

			argarray.at(0) = (char*)romPath[previousUsedDevice].c_str();

			char game_TID[5];

			FILE *f_nds_file = fopen(filename.c_str(), "rb");
			fseek(f_nds_file, offsetof(sNDSHeaderExt, gameCode), SEEK_SET);
			fread(game_TID, 1, 4, f_nds_file);
			game_TID[4] = 0;

			fclose(f_nds_file);

			loadPerGameSettings(filename);

			int runNds_language = perGameSettings_language == -2 ? gameLanguage : perGameSettings_language;
			int runNds_gameRegion = perGameSettings_region == -1 ? gameRegion : perGameSettings_region;

			// Set region flag
			if (runNds_gameRegion == -2 && game_TID[3] != 'A' && game_TID[3] != '#') {
				if (game_TID[3] == 'J') {
					*(u8*)(0x02FFFD70) = 0;
				} else if (game_TID[3] == 'E' || game_TID[3] == 'T') {
					*(u8*)(0x02FFFD70) = 1;
				} else if (game_TID[3] == 'P' || game_TID[3] == 'V') {
					*(u8*)(0x02FFFD70) = 2;
				} else if (game_TID[3] == 'U') {
					*(u8*)(0x02FFFD70) = 3;
				} else if (game_TID[3] == 'C') {
					*(u8*)(0x02FFFD70) = 4;
				} else if (game_TID[3] == 'K') {
					*(u8*)(0x02FFFD70) = 5;
				}
			} else if (runNds_gameRegion == -1 || (runNds_gameRegion == -2 && (game_TID[3] == 'A' || game_TID[3] == '#'))) {
				u8 country = *(u8*)0x02000405;
				if (country == 0x01) {
					*(u8*)(0x02FFFD70) = 0;	// Japan
				} else if (country == 0xA0) {
					*(u8*)(0x02FFFD70) = 4;	// China
				} else if (country == 0x88) {
					*(u8*)(0x02FFFD70) = 5;	// Korea
				} else if (country == 0x41 || country == 0x5F) {
					*(u8*)(0x02FFFD70) = 3;	// Australia
				} else if ((country >= 0x08 && country <= 0x34) || country == 0x99 || country == 0xA8) {
					*(u8*)(0x02FFFD70) = 1;	// USA
				} else if (country >= 0x40 && country <= 0x70) {
					*(u8*)(0x02FFFD70) = 2;	// Europe
				}
			} else {
				*(u8*)(0x02FFFD70) = runNds_gameRegion;
			}

			if (runNds_language >= 0 && runNds_language <= 7 && *(u8*)0x02000406 != runNds_language) {
				tonccpy((char*)0x02000600, (char*)0x02000400, 0x200);
				*(u8*)0x02000606 = runNds_language;
				*(u32*)0x02FFFDFC = 0x02000600;
			}

			bool useWidescreen = (perGameSettings_wideScreen == -1 ? wideScreen : perGameSettings_wideScreen);

			if (consoleModel >= 2 && twlBgCxiFound && useWidescreen && homebrewHasWide) {
				argarray.push_back((char*)"wide");
			}

			bool runNds_boostCpu = perGameSettings_boostCpu == -1 ? boostCpu : perGameSettings_boostCpu;
			bool runNds_boostVram = perGameSettings_boostVram == -1 ? boostVram : perGameSettings_boostVram;

			return runNdsFile (argarray[0], argarray.size(), (const char **)&argarray[0], true, true, (!perGameSettings_dsiMode ? true : false), runNds_boostCpu, runNds_boostVram, runNds_language);
		} case 3: {
			if (dsiWareBooter || consoleModel >= 2) {
				if (homebrewBootstrap) {
					unlaunchBootDSiWare();
				} else {
					romfolder = romPath[previousUsedDevice];
					while (!romfolder.empty() && romfolder[romfolder.size()-1] != '/') {
						romfolder.resize(romfolder.size()-1);
					}
					chdir(romfolder.c_str());

					filename = romPath[previousUsedDevice];
					const size_t last_slash_idx = filename.find_last_of("/");
					if (std::string::npos != last_slash_idx)
					{
						filename.erase(0, last_slash_idx + 1);
					}

					loadPerGameSettings(filename);
					bool useNightly = (perGameSettings_bootstrapFile == -1 ? bootstrapFile : perGameSettings_bootstrapFile);
					if (*(u32*)(0x02000000) & BIT(3)) {
						useNightly = *(bool*)(0x02000010);
					}

					bool useWidescreen = (perGameSettings_wideScreen == -1 ? wideScreen : perGameSettings_wideScreen);
					if (*(u32*)(0x02000000) & BIT(3)) {
						useWidescreen = *(bool*)(0x02000014);
					}

					wideCheck(useWidescreen);

					char ndsToBoot[256];
					sprintf(ndsToBoot, "sd:/_nds/nds-bootstrap-%s.nds", useNightly ? "nightly" : "release");
					if(access(ndsToBoot, F_OK) != 0) {
						sprintf(ndsToBoot, "fat:/_nds/nds-bootstrap-%s.nds", useNightly ? "nightly" : "release");
					}

					argarray.at(0) = (char *)ndsToBoot;

				  if (!(*(u32*)(0x02000000) & BIT(3))) {
					char sfnSrl[62];
					char sfnPub[62];
					char sfnPrv[62];
					if (previousUsedDevice && dsiWareToSD) {
						if (access("sd:/_nds/TWiLightMenu/tempDSiWare.pub.bak", F_OK) == 0) {
							if (access("sd:/_nds/TWiLightMenu/tempDSiWare.pub", F_OK) == 0) {
								remove("sd:/_nds/TWiLightMenu/tempDSiWare.pub");
							}
							rename("sd:/_nds/TWiLightMenu/tempDSiWare.pub.bak", "sd:/_nds/TWiLightMenu/tempDSiWare.pub");
						}
						if (access("sd:/_nds/TWiLightMenu/tempDSiWare.prv.bak", F_OK) == 0) {
							if (access("sd:/_nds/TWiLightMenu/tempDSiWare.prv", F_OK) == 0) {
								remove("sd:/_nds/TWiLightMenu/tempDSiWare.prv");
							}
							rename("sd:/_nds/TWiLightMenu/tempDSiWare.prv.bak", "sd:/_nds/TWiLightMenu/tempDSiWare.prv");
						}
						fatGetAliasPath("sd:/", "sd:/_nds/TWiLightMenu/tempDSiWare.dsi", sfnSrl);
						fatGetAliasPath("sd:/", "sd:/_nds/TWiLightMenu/tempDSiWare.pub", sfnPub);
						fatGetAliasPath("sd:/", "sd:/_nds/TWiLightMenu/tempDSiWare.prv", sfnPrv);
					} else {
						fatGetAliasPath(previousUsedDevice ? "fat:/" : "sd:/", dsiWareSrlPath.c_str(), sfnSrl);
						fatGetAliasPath(previousUsedDevice ? "fat:/" : "sd:/", dsiWarePubPath.c_str(), sfnPub);
						fatGetAliasPath(previousUsedDevice ? "fat:/" : "sd:/", dsiWarePrvPath.c_str(), sfnPrv);
					}

					CIniFile bootstrapini(bootstrapinipath);
					bootstrapini.SetString("NDS-BOOTSTRAP", "NDS_PATH", previousUsedDevice && dsiWareToSD ? "sd:/_nds/TWiLightMenu/tempDSiWare.dsi" : dsiWareSrlPath);
					bootstrapini.SetString("NDS-BOOTSTRAP", "APP_PATH", sfnSrl);
					bootstrapini.SetString("NDS-BOOTSTRAP", "SAV_PATH", sfnPub);
					bootstrapini.SetString("NDS-BOOTSTRAP", "PRV_PATH", sfnPrv);
					bootstrapini.SetString("NDS-BOOTSTRAP", "AP_FIX_PATH", "");
					// bootstrapini.SetString("NDS-BOOTSTRAP", "GUI_LANGUAGE", ms().getGuiLanguageString());
					bootstrapini.SetInt("NDS-BOOTSTRAP", "LANGUAGE", (perGameSettings_language == -2 ? gameLanguage : perGameSettings_language));
					bootstrapini.SetInt("NDS-BOOTSTRAP", "REGION", 	(perGameSettings_region == -3 ? gameRegion : perGameSettings_region));
					bootstrapini.SetInt("NDS-BOOTSTRAP", "DSI_MODE", true);
					bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_CPU", true);
					bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_VRAM", true);
					bootstrapini.SetInt("NDS-BOOTSTRAP", "DONOR_SDK_VER", 5);
					bootstrapini.SetInt("NDS-BOOTSTRAP", "GAME_SOFT_RESET", 1);
					bootstrapini.SetInt("NDS-BOOTSTRAP", "PATCH_MPU_REGION", 0);
					bootstrapini.SetInt("NDS-BOOTSTRAP", "PATCH_MPU_SIZE", 0);
					bootstrapini.SaveIniFile(bootstrapinipath);
				  }

					return runNdsFile(argarray[0], argarray.size(), (const char **)&argarray[0], true, true, false, true, true, -1);
				}
			} else {
				const char *typeToReplace = ".nds";
				if (extention(filename, ".dsi")) {
					typeToReplace = ".dsi";
				} else if (extention(filename, ".ids")) {
					typeToReplace = ".ids";
				} else if (extention(filename, ".srl")) {
					typeToReplace = ".srl";
				} else if (extention(filename, ".app")) {
					typeToReplace = ".app";
				}

				// Move .pub and/or .prv out of "saves" folder
				std::string pubnameUl = ReplaceAll(filename, typeToReplace, ".pub");
				std::string prvnameUl = ReplaceAll(filename, typeToReplace, ".prv");
				std::string pubpathUl = romfolder + "/" + pubnameUl;
				std::string prvpathUl = romfolder + "/" + prvnameUl;
				if (access(dsiWarePubPath.c_str(), F_OK) == 0)
				{
					rename(dsiWarePubPath.c_str(), pubpathUl.c_str());
				}
				if (access(dsiWarePrvPath.c_str(), F_OK) == 0)
				{
					rename(dsiWarePrvPath.c_str(), prvpathUl.c_str());
				}

				unlaunchBootDSiWare();
			}
			break;
		} case 4:
			argarray.at(0) = (char*)"sd:/_nds/TWiLightMenu/emulators/nestwl.nds";
			return runNdsFile (argarray[0], argarray.size(), (const char **)&argarray[0], true, true, false, true, true, -1);	// Pass ROM to nesDS as argument
		case 5:
			argarray.at(0) = (char*)"sd:/_nds/TWiLightMenu/emulators/gameyob.nds";
			return runNdsFile (argarray[0], argarray.size(), (const char **)&argarray[0], true, true, false, true, true, -1);	// Pass ROM to GameYob as argument
		case 6:
			mkdir("sd:/data", 0777);
			mkdir("sd:/data/s8ds", 0777);
			argarray.at(0) = (char*)"sd:/_nds/TWiLightMenu/emulators/S8DS.nds";
			return runNdsFile(argarray[0], argarray.size(), (const char **)&argarray[0], true, true, false, true, true, -1); // Pass ROM to S8DS as argument
		case 7:
			argarray.at(0) = (char*)"sd:/_nds/TWiLightMenu/apps/RocketVideoPlayer.nds";
			return runNdsFile (argarray[0], argarray.size(), (const char **)&argarray[0], true, true, false, true, true, -1);	// Pass video to Rocket Video Player as argument
		case 8:
			argarray.at(0) = (char*)"sd:/_nds/TWiLightMenu/apps/FastVideoDS.nds";
			return runNdsFile (argarray[0], argarray.size(), (const char **)&argarray[0], true, true, false, true, true, -1);	// Pass video to FastVideoDS as argument
		case 9:
			argarray.at(0) = (char*)"sd:/_nds/TWiLightMenu/emulators/StellaDS.nds";
			return runNdsFile (argarray[0], argarray.size(), (const char **)&argarray[0], true, true, false, true, true, -1);	// Pass ROM to StellaDS as argument
		case 10:
			argarray.at(0) = (char*)"sd:/_nds/TWiLightMenu/emulators/PicoDriveTWL.nds";
			return runNdsFile (argarray[0], argarray.size(), (const char **)&argarray[0], true, true, false, true, true, -1);	// Pass ROM to PicoDrive TWL as argument
		case 12:
			argarray.at(0) = (char*)"sd:/_nds/TWiLightMenu/emulators/A7800DS.nds";
			return runNdsFile (argarray[0], argarray.size(), (const char **)&argarray[0], true, true, false, true, true, -1);	// Pass ROM to A7800DS as argument
		case 13:
			argarray.at(0) = (char*)"sd:/_nds/TWiLightMenu/emulators/A5200DS.nds";
			return runNdsFile (argarray[0], argarray.size(), (const char **)&argarray[0], true, true, false, true, true, -1);	// Pass ROM to A5200DS as argument
		case 14:
			mkdir("sd:/data", 0777);
			mkdir("sd:/data/NitroGrafx", 0777);
			argarray.at(0) = (char*)"sd:/_nds/TWiLightMenu/emulators/NitroGrafx.nds";
			return runNdsFile (argarray[0], argarray.size(), (const char **)&argarray[0], true, true, false, true, true, -1);	// Pass ROM to NitroGrafx as argument
		case 15:
			argarray.at(0) = (char*)"sd:/_nds/TWiLightMenu/emulators/XEGS-DS.nds";
			return runNdsFile (argarray[0], argarray.size(), (const char **)&argarray[0], true, true, false, true, true, -1);	// Pass ROM to XEGS-DS as argument
	}
	
	return -1;
}

//---------------------------------------------------------------------------------
int main(int argc, char **argv) {
//---------------------------------------------------------------------------------

	// overwrite reboot stub identifier
	extern char *fake_heap_end;
	*fake_heap_end = 0;

	defaultExceptionHandler();

	if (!fatInitDefault()) {
		consoleDemoInit();
		printf("fatInitDefault failed!");
		stop();
	}

	if (*(u32*)(0x02000004) != 0) {
		*(u32*)(0x02000000) = 0; // Clear soft-reset params
	}

	flashcardInit();

	if (!flashcardFound()) {
		disableSlot1();
	}

	fifoWaitValue32(FIFO_USER_06);

	int err = lastRunROM();
	consoleDemoInit();
	iprintf ("Start failed. Error %i", err);
	stop();

	return 0;
}
