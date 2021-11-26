// dllmain.cpp : Defines the entry point for the DLL application.
#include <pe/module.h>
#include <fnv1a.h>
#include <xorstr.hpp>
#include <regex>
#include "pluginsdk.h"
#include "searchers.h"
#include <tchar.h>

// XML stuff
#include <pugixml.hpp>
#include <ShlObj.h>
#include <KnownFolders.h>
#include <codecvt>
#include <filesystem>
#include <fstream>
#include <list>
#include <map>
#include <string_view>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <wil/stl.h>
#include <wil/win32_helpers.h>
#include <iostream>

// MS Detours
#include "detours.h"

pugi::xml_document CfgDoc;
static std::filesystem::path docPath;
static int useDebug = 0;
static float desiredSpeed = 0.0f;

#define thiscall_(name, thisarg, ...) name(thisarg, ## __VA_ARGS__) 

std::string skill_query_all_str = "/config/gcd/skill[@id='0']";
pugi::xpath_query skill_query_all(skill_query_all_str.c_str());

const std::filesystem::path& documents_path()
{
	static std::once_flag once_flag;
	static std::filesystem::path path;

	std::call_once(once_flag, [](std::filesystem::path& path) {
		wil::unique_cotaskmem_string result;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &result)))
			path = result.get();
		}, path);
	return path;
}

const std::string currentTime() {
	time_t now = time(0);
	struct tm tstruct;
	char buf[80];
	tstruct = *localtime(&now);
	strftime(buf, sizeof(buf), "%m/%d/%Y %X", &tstruct);
	return buf;
}

const void CLog(std::string msg) {
	std::cout << "[" << currentTime() << "] " << msg << std::endl;
}

bool(__fastcall* oCheckAutoCombatZone)(INT64);
bool _fastcall hkCheckAutoCombatZone(INT64 a1) {
	return (unsigned __int8)1;
}

bool(__fastcall* oFishPasteType)();
bool _fastcall hkFishPasteType() {
	return (unsigned __int8)1;
}

bool(__fastcall* oSwitchControlMode)(__int64);
bool __fastcall hkSwitchControlMode(__int64 a1)
{
	CfgDoc.load_file(docPath.c_str(), pugi::parse_default);
	return oSwitchControlMode(a1);
}

bool(__fastcall* oStaminDrainMode)(__int64, char, __int64);
bool __fastcall hkoStaminaDrainMode(__int64 a1, char a2, __int64 a3) {

	if ((*(float*)(a1 + 0x38) == 2000.0))
		*(float*)(a1 + 0x38) = 0.05;
	return oStaminDrainMode(a1, a2, a3);
}

bool(__fastcall* oStaminMonitor)(__int64, __int64, float, float);
bool _fastcall hkStaminaMonitor(__int64 vtable, __int64 a2, float a3, float a4) {

	__int64 charData = vtable + 0x4C8;
	if (charData) {
		if ((*reinterpret_cast<float*>(charData + 0x38)) != 0.05)
			*reinterpret_cast<float*>(charData + 0x38) = 0.05f;
	}

	return oStaminMonitor(vtable, a2, a3, a4);
}

class LocalPlayer {
public:
	char pad_0000[0x246C]; //0x0000
	float X_Coord_1; //0x246C
	float Y_Coord_1; //0x2470
	float Z_Coord_1; //0x2474
	char pad_2478[0x7AC]; //0x2478
	float groundSpeed; //0x2C24
	float waterSpeed; //0x2C28
	float airSpeed; //0x2C2C
	float Unknown; //0x2C30
	float Unknown_2; //0x2C34
	float jumpHeight; //0x2C38
};

class LocalMount {
public:
	char Padding_1[0x18C];
	float groundSpeed;
	char Padding_2[0x8];
	float waterSpeed;
	float airSpeed;
};

bool(__fastcall* oLocalPlayerSpeedController)(LocalPlayer*, unsigned char*, unsigned char*, float*, float*);
bool __fastcall hkLocalPlayerSpeedController(LocalPlayer* localPlayer, unsigned char* a2, unsigned char* a3, float* a4, float* a5) {

	if (localPlayer->groundSpeed != 0 && localPlayer->groundSpeed < desiredSpeed)
		localPlayer->groundSpeed = desiredSpeed;

	return oLocalPlayerSpeedController(localPlayer, a2, a3, a4, a5);
}

bool(__fastcall* oLocalMountController)(__int64, float);
bool __fastcall hkLocalMountController(__int64 localMount, float a2) {

	LocalMount* mount = reinterpret_cast<LocalMount*>(localMount);
	if (mount->groundSpeed != 0 && mount->groundSpeed < desiredSpeed) mount->groundSpeed = desiredSpeed;

	return oLocalMountController(localMount, a2);
}

bool(__fastcall* oGCDHook)(signed int*, __int64, int, float);
bool __fastcall hkGCDReader(signed int* a1, __int64 skillID, int group, float GCD) {
	std::string searchStr = "/config/gcd/skill[@id='" + std::to_string((int)skillID) + "']";
	pugi::xpath_query skill_query(searchStr.c_str());
	pugi::xpath_node xpathNode = CfgDoc.select_node(skill_query);
	pugi::xpath_node all_skills = CfgDoc.select_node(skill_query_all);

	if (useDebug)
		std::cout << "Skill ID: " << (int)skillID << std::endl << "GCD: " << GCD << std::endl << "Group: " << group << std::endl;

	if (xpathNode) {
		if (!xpathNode.node().attribute("mode").as_bool()) {
			float skillFloatXML = xpathNode.node().attribute("value").as_float();
			GCD += skillFloatXML;
		}
		else {
			GCD = (float)(xpathNode.node().attribute("value").as_int() / 1000.0);
		}
		/*
		int xgroup = xpathNode.node().attribute("group").as_int(-1);
		if (xgroup > -1)
			group = xgroup;
		*/
	}
	else {
		if (all_skills) {
			if (!all_skills.node().attribute("mode").as_bool()) {
				float skillFloatXML = all_skills.node().attribute("value").as_float();
				GCD += skillFloatXML;
			}
			else {
				GCD = (float)(all_skills.node().attribute("value").as_int() / 1000.0);
			}
		}
	}

	if (GCD < 0.0f)
		GCD = 0.0f;

	if (useDebug)
		std::cout << "Modified GCD: " << GCD << std::endl << std::endl;

	return oGCDHook(a1, skillID, group, GCD);
}

bool initdone = false;
void __cdecl oep_notify([[maybe_unused]] const Version client_version)
{
	if (initdone)
		return;

	if (const auto module = pe::get_module()) {
		initdone = true;

		DetourTransactionBegin();
		DetourUpdateThread(NtCurrentThread());

		uintptr_t handle = module->handle();
		//bool isUE4 = module->base_name() == L"BNSR.exe" ? true : false;
		const auto sections2 = module->segments();
		const auto& s2 = std::find_if(sections2.begin(), sections2.end(), [](const IMAGE_SECTION_HEADER& x) {
			return x.Characteristics & IMAGE_SCN_CNT_CODE;
			});
		const auto data = s2->as_bytes();

		pugi::xml_parse_result loadResult = CfgDoc.load_file(docPath.c_str(), pugi::parse_default);

		//Failed to load XML document, abort like my mother should of done with me.
		if (!loadResult)
		{
			MessageBox(NULL, L"Failed to load multitool_qol xml", L"Load Failure", MB_OK);
			return;
		}

		/*
			Retrieve options via multitool_qol.xml in Documents\BnS
			This section is not used if it fails to load the xml
		*/
		DWORD oldprotect;
		bool useAutoCombat = (CfgDoc.select_node("/config/options/option[@name='AutoCombat']").node().attribute("enable").as_bool());
		bool useAutoBait = (CfgDoc.select_node("/config/options/option[@name='useAutoBait']").node().attribute("enable").as_bool());
		bool useItemCap = (CfgDoc.select_node("/config/options/option[@name='useItemCap']").node().attribute("enable").as_bool());
		bool useMarketplace = (CfgDoc.select_node("/config/options/option[@name='useMarketplace']").node().attribute("enable").as_bool());
		bool useAutoCombatRange = (CfgDoc.select_node("/config/options/option[@name='AutoCombat']").node().attribute("useRange").as_bool());
		bool useNoCameraLock = (CfgDoc.select_node("/config/options/option[@name='useNoCameraLock']").node().attribute("enable").as_bool());
		bool useNoWallRunStamina = (CfgDoc.select_node("/config/options/option[@name='useNoWallRunStamina']").node().attribute("enable").as_bool());
		useDebug = (CfgDoc.select_node("/config/options/option[@name='useDebug']").node().attribute("enable").as_int());
		bool enableGCD = (CfgDoc.select_node("/config/gcd").node().attribute("enable").as_bool());

		if (useDebug == 1) {
			AllocConsole();
			freopen("CONOUT$", "w", stdout);
		}

		/*
			Auto Combat enablation
		*/
		if (useAutoCombat) {
			//Hook the function that checks the zone rule for if Auto Combat is allowed in this zone.
			CLog("Searching for AutoCombatZoneRuleCheck");
			auto sAutoCombat = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("41 FF 90 08 01 00 00 90 40 0F B6 C6 48 8B 5C 24 60 48 8B 6C 24 68 48 8B 74 24 70 48 83 C4 40 41 5F")));
			uintptr_t aAutoCombat = NULL;
			if (sAutoCombat != data.end()) {
				CLog("Hooking AutoCombatZoneRuleCheck");
				aAutoCombat = (uintptr_t)&sAutoCombat[0] - 0x203;
				oCheckAutoCombatZone = module->rva_to<std::remove_pointer_t<decltype(oCheckAutoCombatZone)>>(aAutoCombat - handle);
				DetourAttach(&(PVOID&)oCheckAutoCombatZone, &hkCheckAutoCombatZone);
			}
			else if (useDebug)
				CLog("Failed to hook AutoCombatZoneRuleCheck");
			else
				MessageBox(NULL, L"Could not find the function for auto combat feature not enabled", L"Search Error", MB_OK);
		}

		/*
			Auto combat Range
		*/
		if (useAutoCombatRange) {
			/*
				Find the master function for auto-combat
				Adjust pointer the value will be loaded from to
				a point in the memory where we will be setting our
				custom value.

				Allocation Point: BNSR.exe + 0x900
				This point of the memory is always free and never used.
				We can freely use this section of memory without allocating a 
				new chunk, just need to adjust the memory permissions for this
				sector.
			*/
			CLog("Searching for AutoCombatRange");
			auto sAutoCombatRange = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("F3 0F 10 90 98 00 00 00 0F 28 C2")));
			uintptr_t aAutoCombatRange = NULL;
			if (sAutoCombatRange != data.end()) {
				CLog("Hooking AutoCombatRange");
				aAutoCombatRange = (uintptr_t)&sAutoCombatRange[0];
				int customRange = (CfgDoc.select_node("/config/options/option[@name='AutoCombat']").node().attribute("range").as_int());
				float autoModeRange = customRange * static_cast<float>(100) / 2;

				uintptr_t allocPoint = (uintptr_t)GetModuleHandle(L"BNSR.exe") + 0x900;
				VirtualProtect((LPVOID)allocPoint, 50, PAGE_EXECUTE_READWRITE, &oldprotect);
				*(float*)(allocPoint + 0x19) = autoModeRange;

				BYTE shellcode[] = {
					0xF3, 0x0F, 0x10, 0x15, 0x00, 0x00, 0x00, 0x00
				};

				//Floataddr - functionaddr - length of instruction
				*reinterpret_cast<uintptr_t*>(shellcode + 0x4) = ((allocPoint + 0x19) - aAutoCombatRange - 8);
				for (int i = 0; i <= 8; i++) {
					if (shellcode[i] == 0xFF)
						shellcode[i] = 0x90;
				}

				VirtualProtect((LPVOID)aAutoCombatRange, sizeof(shellcode), PAGE_EXECUTE_READWRITE, &oldprotect);
				memcpy((LPVOID)aAutoCombatRange, shellcode, 8);
				VirtualProtect((LPVOID)aAutoCombatRange, sizeof(shellcode), oldprotect, &oldprotect);
			}
			else if (useDebug)
				CLog("Failed to AutoCombatRange");
			else
				MessageBox(NULL, L"Could not find the function for auto combat range feature not enabled", L"Search Error", MB_OK);
		}

		/*
			Auto Bait enablation
		*/
		if (useAutoBait) {
			// Hook the function responsible for parsing what type of bait the fishing paste is.
			CLog("Searching for FishPasteType");
			auto sAutoBait = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("48 8B 88 E4 37 00 00 48 8B 01 FF 90 B8 00 00 00 48 8B D8 48 89 44 24 38 BD FF FF FF FF 8B F5 89 6C 24 40")));
			uintptr_t aAutoBait = NULL;
			if (sAutoBait != data.end()) {
				CLog("Hooking FishPasteType");
				aAutoBait = (uintptr_t)&sAutoBait[0] - 0x25;
				oFishPasteType = module->rva_to<std::remove_pointer_t<decltype(oFishPasteType)>>(aAutoBait - handle);
				DetourAttach(&(PVOID&)oFishPasteType, &hkFishPasteType);
			}
			else if (useDebug)
				CLog("Failed to hook FishPasteType");
			else
				MessageBox(NULL, L"Could not find the function for fish baits feature not enabled", L"Search Error", MB_OK);
		}

		/*
			Raise Item Cap
		*/
		if (useItemCap) {
			/*
				Searches for the function responsible for setting the received-items limit
				It is a hard-coded value within the function setting it to 100, we adjust the bytes
				to say the limit is 10,000
			*/
			CLog("Searching for MaxItem: 100");
			auto sItemCap = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("41 BE 64 00 00 00 41 3B C6")));
			uintptr_t aItemCap = NULL;
			if (sItemCap != data.end()) {
				CLog("Raising MaxItem: 10,000");
				aItemCap = (uintptr_t)&sItemCap[0];
				BYTE bItemCap[] = { 0x41, 0xBE, 0x10, 0x27, 0x00, 0x00 };
				VirtualProtect((LPVOID)aItemCap, sizeof(bItemCap), PAGE_EXECUTE_READWRITE, &oldprotect);
				memcpy((LPVOID)aItemCap, bItemCap, sizeof(bItemCap));
				VirtualProtect((LPVOID)aItemCap, sizeof(bItemCap), oldprotect, &oldprotect);
			}
			else if (useDebug)
				CLog("Failed to find MaxItem: 100");
			else
				MessageBox(NULL, L"Could not find the function for mailbox items feature not enabled", L"Search Error", MB_OK);
		}

		/*
			Marketplace in cross-server
		*/
		if (useMarketplace) {
			/*
				Find the function responsible for handling the opening of the marketplace
				Adjusts a jump condition specifically when for when player is not local-bound
			*/
			CLog("Searching for Marketplace Restriction");
			auto sMarketplace = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("4C 8B 0D ?? ?? ?? ?? 4D 85 C9 74 ?? 41 80 79 70 01")));
			uintptr_t aMarketplace = NULL;
			if (sMarketplace != data.end()) {
				CLog("Removing Marketplace Restriction");
				aMarketplace = (uintptr_t)&sMarketplace[0] + 0xA;
				BYTE bMarketplace[] = { 0xEB };
				VirtualProtect((LPVOID)aMarketplace, sizeof(bMarketplace), PAGE_EXECUTE_READWRITE, &oldprotect);
				memcpy((LPVOID)aMarketplace, bMarketplace, sizeof(bMarketplace));
				VirtualProtect((LPVOID)aMarketplace, sizeof(bMarketplace), oldprotect, &oldprotect);
			}
			else if (useDebug)
				CLog("Failed to remove Marketplace Restriction");
			else
				MessageBox(NULL, L"Could not find the function for marketplace feature not enabled", L"Search Error", MB_OK);
		}

		if (useNoCameraLock) {
			// Hook the function responsible for hnadling the locking state of a players camera
			auto sCameraLock = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("33 C0 41 FF C8 0F B6 D2 45 8D 04 50 45 85 C0 44 0F 4E C0 44 89 81 28 03 00 00")));
			CLog("Searching for CameraLock");
			uintptr_t aCameraLock = NULL;
			if (sCameraLock != data.end()) {
				CLog("Jumping CameraLock");
				aCameraLock = (uintptr_t)&sCameraLock[0] + 0x1E;
				BYTE bytes[] = { 0xEB };
				VirtualProtect((LPVOID)aCameraLock, sizeof(bytes), PAGE_EXECUTE_READWRITE, &oldprotect);
				memcpy((LPVOID)aCameraLock, bytes, sizeof(bytes));
				VirtualProtect((LPVOID)aCameraLock, sizeof(bytes), oldprotect, &oldprotect);
			}
			else if (useDebug)
				CLog("Failed to find CameraLock");
			else
				MessageBox(NULL, L"Could not find the function for camera locking feature not enabled", L"Search Error", MB_OK);
		}

		if (useNoWallRunStamina) {
			CLog("Searching for StaminaConsumption");
			/*
				A function called when the player is 'sprinting' and handles the stamina consumption
				We hook this to accurately get the pointer to the stamina structure.
			*/
			auto sStaminaConsumption = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("48 C7 45 A0 FE FF FF FF 48 89 58 10 48 89 70 18 48 89 78 20 0F 29 70 E8 0F 29 78 D8")));
			uintptr_t aStaminaConsumption = NULL;
			if (sStaminaConsumption != data.end()) {
				CLog("Hooking StaminaConsumption");
				aStaminaConsumption = (uintptr_t)&sStaminaConsumption[0] - 0x12;
				oStaminMonitor = module->rva_to<std::remove_pointer_t<decltype(oStaminMonitor)>>(aStaminaConsumption - handle);
				DetourAttach(&(PVOID&)oStaminMonitor, &hkStaminaMonitor);
			}
			else if (useDebug)
				CLog("Failed to hook StaminaConsumption");
			else
				MessageBox(NULL, L"Could not find function for Stamina feature not enabled", L"Search Error", MB_OK);
		}

		if (enableGCD) {
			CLog("Searching for GlobalCooldownHandler");
			//One of the final end-points where a skills GCD value is parsed, offset by delay between server and other things.
			auto sGCDHook = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("48 89 5C 24 10 48 89 74 24 20 57 48 81 EC 90 00 00 00 8B 01 41 8B D8 0F 29 B4 24 80 00 00 00")));
			uintptr_t aGCDHook = NULL;
			if (sGCDHook != data.end()) {
				CLog("Hooking GlobalCooldownHandler");
				aGCDHook = (uintptr_t)&sGCDHook[0];
				oGCDHook = module->rva_to<std::remove_pointer_t<decltype(oGCDHook)>>(aGCDHook - handle);
				DetourAttach(&(PVOID&)oGCDHook, &hkGCDReader);

				//48 89 4C 24 08 53 41 56 48 81 EC 98 00 00 00
				//Hook used to act as a in-game reload for GCD, Function is called on P (Character Info Menu) and for whatever reason also called during load screens..
				CLog("Searching for CharacterInfo Menu");
				auto sSwitchControlMode = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("48 8B C4 55 41 54 41 55 41 56 41 57 48 8D A8 38 FF FF FF 48 81 EC C0 01 00 00 48 C7 45 90 FE FF FF FF")));
				uintptr_t aSwitchControlMode = NULL;
				if (sSwitchControlMode != data.end()) {
					CLog("Hooking CharacterInfo Menu");
					aSwitchControlMode = (uintptr_t)&sSwitchControlMode[0];
					oSwitchControlMode = module->rva_to<std::remove_pointer_t<decltype(oSwitchControlMode)>>(aSwitchControlMode - handle);
					DetourAttach(&(PVOID&)oSwitchControlMode, &hkSwitchControlMode);
				}
				else if (useDebug)
					CLog("Failed to hook CharacterInfo Menu (P reloading of GCD will not work)");
			}
			else if (useDebug)
				CLog("Failed to hook GlobalCooldownHandler");
			else
				MessageBox(NULL, L"Could not find function for GCD feature not enabled", L"Search Error", MB_OK);
		}

		DetourTransactionCommit();
		CLog("Systems Initialized");
	}
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		if (docPath.empty())
			docPath = documents_path() / xorstr_(L"BnS\\multitool_qol.xml");

		DisableThreadLibraryCalls(hInstance);
	}

	return TRUE;
}

bool __cdecl init([[maybe_unused]] const Version client_version)
{
	NtCurrentPeb()->BeingDebugged = FALSE;
	return true;
}


extern "C" __declspec(dllexport) PluginInfo GPluginInfo = {
  .hide_from_peb = true,
  .erase_pe_header = true,
  .init = init,
  .oep_notify = oep_notify,
  .priority = 1,
  .target_apps = L"BNSR.exe"
};