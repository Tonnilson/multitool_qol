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

// Game Engine Stuff
#include "BT_AutoMode.h"
#include "defs.h"
#include "UIEngineInterface.h"


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

// Why tf did I write std::string it should be char* but too lazy to fix
const void CLog(std::string msg) {
	wchar_t szBuffer[1024];
	wsprintf(szBuffer, xorstr_(L"[MULTITOOL_QOL] %S\n"), msg.c_str());
	WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), szBuffer, wcslen(szBuffer), NULL, NULL);
}

/*
	Why am I doing this to write to console instead of redirecting stdout stream? Some fucking moron at NC
	decided that python was absolutely needed and decided to mix it into the environment so now when redirecting
	stdout you will randomly cause a crash when the python stuff initializes which also crashes the entire process.
*/
void ConsoleWrite(const wchar_t* msg, ...) {
	wchar_t szBuffer[1024];
	va_list args;
	va_start(args, msg);
	vswprintf(szBuffer, 1024, msg, args);
	WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), szBuffer, wcslen(szBuffer), NULL, NULL);
	va_end(args);
}

int GetKeyCodeFromString(const pugi::char_t* str) {
	std::stringstream vk;
	int keyCode = NULL;
	vk << std::hex << str;
	vk >> keyCode;
	return keyCode;
}


bool(__fastcall* oCheckAutoCombatZone)(INT64);
bool _fastcall hkCheckAutoCombatZone(INT64 a1) {
	if (!bAutoCombatEverywhere)
		return oCheckAutoCombatZone(a1);
	else
		return (unsigned __int8)1;
}

bool(__fastcall* oIsAutomaticFishingPasteItem)();
bool _fastcall hkIsAutomaticFishingPasteItem() {
	if (!bAutoBait)
		return oIsAutomaticFishingPasteItem();
	else
		return (unsigned __int8)1;
}

bool(__fastcall* oStaminMonitor)(__int64, __int64, float, float);
bool _fastcall hkStaminaMonitor(__int64 BPTPlayer, __int64 a2, float a3, float a4)
{
	if (!bNoWallRunStamina)
		return oStaminMonitor(BPTPlayer, a2, a3, a4);

	__int64 HyperMove = BPTPlayer + HyperMoveOffset;
	if (HyperMove) {
		if ((*reinterpret_cast<float*>(HyperMove + 0x38)) != 0.05)
			*reinterpret_cast<float*>(HyperMove + 0x38) = 0.05f;
		/*
		* ptr + 8 * 5 + 10 = 38
		// HyperMoveType: 5 ( Wall running )
		if ((*reinterpret_cast<byte*>(HyperMove + 0x4C)) == 5) {
			if ((*reinterpret_cast<float*>((HyperMove + 8 * *(signed int*)(HyperMove + 0x4C) + 0x10))) != 0.05)
				*reinterpret_cast<float*>((HyperMove + 8 * *(signed int*)(HyperMove + 0x4C) + 0x10)) = 0.05f;
		}
		*/
	}

	return oStaminMonitor(BPTPlayer, a2, a3, a4);
}

// I should probably just pass the buffer in the params instead of returning this buffer
std::string GetPingMode() {
	std::string buffer;
	switch (bIgnorePing) {
	case GCDPing::Enabled:
		buffer = xorstr_("Both");
		break;
	case GCDPing::GCD_Only:
		buffer = xorstr_("GCD Only");
		break;
	case GCDPing::CD_Only:
		buffer = xorstr_("Recycle Only");
		break;
	default:
		buffer = xorstr_("None");
		break;
	}

	return buffer;
}

bool(__fastcall* oBnsResurrectionTickTask)(__int64, __int64, char*, float);
bool __fastcall hkBnsresurrectionTickTask(__int64 thisptr, __int64 ownerComp, char* nodeMemory, float deltaSeconds)
{
	if (bAutoModeTurnOffOnDeath)
		return 0;
	else
		return oBnsResurrectionTickTask(thisptr, ownerComp, nodeMemory, deltaSeconds);
}

bool(__fastcall* oSetGlobalCoolTime_renewal)(SkillTimer*, __int64, int, float);
bool __fastcall hkSetGlobalCoolTime_renewal(SkillTimer* thisptr, __int64 skillId, int globalGroupIndex, float globalRecycleTime)
{
	if (!bUseCustomGCD)
		return oSetGlobalCoolTime_renewal(thisptr, skillId, globalGroupIndex, globalRecycleTime);

	std::string searchStr = xorstr_("/config/gcd/skill[@id='") + std::to_string((int)skillId) + "']";
	pugi::xpath_query skill_query(searchStr.c_str());
	pugi::xpath_node xpathNode = CfgDoc.select_node(skill_query);
	pugi::xpath_node all_skills = CfgDoc.select_node(skill_query_all);

	if (!BNSInstance)
		BNSInstance = *(BNSClient**)nNet;

	if (useDebug) {
		ConsoleWrite(xorstr_(L"\nPing Rediction: %S\n"), GetPingMode());
		ConsoleWrite(xorstr_(L"Skill ID: %d\n"), (int)skillId);
		ConsoleWrite(xorstr_(L"GlobalCoolTime: %g\n"), globalRecycleTime);
		ConsoleWrite(xorstr_(L"Group: %d\n"), globalGroupIndex);
	}

	if (xpathNode) {
		if (xpathNode.node().attribute(xorstr_("ignoreAutoBias")).as_bool()) {
			thisptr->_coolTimeBias = 0.001;

			if (useDebug)
				ConsoleWrite(xorstr_(L"Ignoring Bias\n"));
		}
		if (!xpathNode.node().attribute(xorstr_("mode")).as_bool()) {
			globalRecycleTime += xpathNode.node().attribute(xorstr_("value")).as_float();
		}
		else {
			globalRecycleTime = (float)(xpathNode.node().attribute(xorstr_("value")).as_int() / 1000.0);
		}
	}
	else {
		if (all_skills) {
			if (all_skills.node().attribute(xorstr_("ignoreAutoBias")).as_bool()) {
				thisptr->_coolTimeBias = 0.001;

				if (useDebug)
					ConsoleWrite(xorstr_(L"Ignoring Bias\n"));
			}

			if (!all_skills.node().attribute(xorstr_("mode")).as_bool()) {
				globalRecycleTime += all_skills.node().attribute(xorstr_("value")).as_float();
			}
			else {
				globalRecycleTime = (float)(all_skills.node().attribute(xorstr_("value")).as_int() / 1000.0);
			}
		}
	}

	if (bIgnorePing == GCDPing::Enabled || bIgnorePing == GCDPing::GCD_Only)
		if (BNSInstance->game && BNSInstance->game->receivedServerPing && BNSInstance->game->receivedServerPing > 0.0f)
			globalRecycleTime -= BNSInstance->game->receivedServerPing / 1000.0;

	if (globalRecycleTime < 0.0f)
		globalRecycleTime = 0.0f;

	if (useDebug)
		ConsoleWrite(xorstr_(L"Modified GCD: %g\n"), globalRecycleTime);
	return oSetGlobalCoolTime_renewal(thisptr, skillId, globalGroupIndex, globalRecycleTime);
}

bool(__fastcall* oSetSkillRecycleTime_renewal)(SkillTimer*, const __int64 skillId, int groupType, int skillGroupIndex, float recycleTime);
bool __fastcall hkSetSkillRecycleTime_renewal(SkillTimer* thisptr, const __int64 skillId, int groupType, int skillGroupIndex, float recycleTime)
{
	if (!bUseCustomGCD)
		return oSetSkillRecycleTime_renewal(thisptr, skillId, groupType, skillGroupIndex, recycleTime);

	std::string searchStr = xorstr_("/config/gcd/skill[@id='") + std::to_string((int)skillId) + "']";
	pugi::xpath_query skill_query(searchStr.c_str());
	pugi::xpath_node xpathNode = CfgDoc.select_node(skill_query);
	pugi::xpath_node all_skills = CfgDoc.select_node(skill_query_all);

	if (!BNSInstance)
		BNSInstance = *(BNSClient**)nNet;

	if (useDebug)
		ConsoleWrite(xorstr_(L"Recycle Time: %g\n"), recycleTime);

	if (xpathNode) {
		if (!xpathNode.node().attribute(xorstr_("recycleMode")).as_bool()) {
			recycleTime += xpathNode.node().attribute(xorstr_("recycleTime")).as_float();
		}
		else {
			recycleTime = (float)(xpathNode.node().attribute(xorstr_("recycleTime")).as_int() / 1000.0);
		}
	}
	else {
		if (all_skills) {
			if (!all_skills.node().attribute(xorstr_("recycleMode")).as_bool()) {
				recycleTime += all_skills.node().attribute(xorstr_("recycleTime")).as_float();
			}
			else {
				recycleTime = (float)(all_skills.node().attribute(xorstr_("recycleTime")).as_int() / 1000.0);
			}
		}
	}

	if (bIgnorePing == GCDPing::Enabled || bIgnorePing == GCDPing::CD_Only)
		if (BNSInstance->game && BNSInstance->game->receivedServerPing && BNSInstance->game->receivedServerPing > 0.0f)
			recycleTime -= BNSInstance->game->receivedServerPing / 1000.0;

	if (useDebug)
		ConsoleWrite(xorstr_(L"Modified Time: %g\n"), recycleTime);

	if (recycleTime < 0.0f)
		recycleTime = 0.0f;

	return oSetSkillRecycleTime_renewal(thisptr, skillId, groupType, skillGroupIndex, recycleTime);
}

bool(__fastcall* oUBTService_BnsFindTargetTickNode)(UBTService_BnsFindTarget* thisptr, uintptr_t* OwnerComp, char* NodeMemory, float DeltaSeconds);
bool __fastcall hkUBTService_BnsFindTargetTickNode(UBTService_BnsFindTarget* thisptr, uintptr_t* OwnerComp, char* NodeMemory, float DeltaSeconds)
{
	if (bAutoCombatRange)
		thisptr->FindDistance = vAutoCombat_Range;
	else
		thisptr->FindDistance = 1500.0f;

	return oUBTService_BnsFindTargetTickNode(thisptr, OwnerComp, NodeMemory, DeltaSeconds);
}

bool ReloadInProgress = false;
bool __fastcall hkBInputKey(BInputKey* thisptr, EInputKeyEvent* InputKeyEvent) {
	if (InputKeyEvent->bAltPressed && InputKeyEvent->_vKey == 0x50) {
		if (!ReloadInProgress && InputKeyEvent->KeyState == EngineKeyStateType::EKS_PRESSED) {
			ReloadInProgress = true;
			CfgDoc.load_file(docPath.c_str(), pugi::parse_default);
			bAutoCombatEverywhere = (CfgDoc.select_node(xorstr_("/config/options/option[@name='AutoCombat']")).node().attribute(xorstr_("enable")).as_bool());
			bAutoBait = (CfgDoc.select_node(xorstr_("/config/options/option[@name='useAutoBait']")).node().attribute(xorstr_("enable")).as_bool());
			bRaiseItemCap = (CfgDoc.select_node(xorstr_("/config/options/option[@name='useItemCap']")).node().attribute(xorstr_("enable")).as_bool());
			bAuctionEverywhere = (CfgDoc.select_node(xorstr_("/config/options/option[@name='useMarketplace']")).node().attribute(xorstr_("enable")).as_bool());
			bAutoCombatRange = (CfgDoc.select_node(xorstr_("/config/options/option[@name='AutoCombat']")).node().attribute(xorstr_("useRange")).as_bool());
			bNoCameraLock = (CfgDoc.select_node(xorstr_("/config/options/option[@name='useNoCameraLock']")).node().attribute(xorstr_("enable")).as_bool());
			bNoWallRunStamina = (CfgDoc.select_node(xorstr_("/config/options/option[@name='useNoWallRunStamina']")).node().attribute(xorstr_("enable")).as_bool());
			bool bOldDebug = useDebug;
			useDebug = (CfgDoc.select_node(xorstr_("/config/options/option[@name='useDebug']")).node().attribute(xorstr_("enable")).as_int());
			bUseCustomGCD = (CfgDoc.select_node(xorstr_("/config/gcd")).node().attribute(xorstr_("enable")).as_bool());
			bIgnorePing = (GCDPing)(CfgDoc.select_node(xorstr_("/config/gcd")).node().attribute(xorstr_("ignorePing")).as_int());
			bAutoModeTurnOffOnDeath = (CfgDoc.select_node(xorstr_("/config/options/option[@name='AutoCombat']")).node().attribute(xorstr_("TurnOffOnDeath")).as_bool());
			bUseWindowClipboard = CfgDoc.select_node(xorstr_("/config/options/option[@name='useWindowClipboard']")).node().attribute(xorstr_("enable")).as_bool();

			if (bAutoCombatRange)
				vAutoCombat_Range = (CfgDoc.select_node(xorstr_("/config/options/option[@name='AutoCombat']")).node().attribute(xorstr_("range")).as_int()) * static_cast<float>(100) / 2;

			if (bOldDebug != useDebug) {
				if (!useDebug) {
					FreeConsole();
				}
				else {
					AllocConsole();
				}
			}

			if (CameraLockCondition) {
				if (*reinterpret_cast<BYTE*>(CameraLockCondition) != bNoCameraLock ? 0xEB : 0x75) {
					DWORD dwProtect;
					VirtualProtect((LPVOID)CameraLockCondition, sizeof(char), PAGE_EXECUTE_READWRITE, &dwProtect);
					*reinterpret_cast<BYTE*>(CameraLockCondition) = bNoCameraLock ? 0xEB : 0x75;
					VirtualProtect((LPVOID)CameraLockCondition, sizeof(char), dwProtect, &dwProtect);
				}
			}

			if (AuctionRestriction) {
				if (*reinterpret_cast<BYTE*>(AuctionRestriction) != bAuctionEverywhere ? 0xEB : 0x74) {
					DWORD dwProtect;
					VirtualProtect((LPVOID)AuctionRestriction, sizeof(char), PAGE_EXECUTE_READWRITE, &dwProtect);
					*reinterpret_cast<BYTE*>(AuctionRestriction) = bAuctionEverywhere ? 0xEB : 0x74;
					VirtualProtect((LPVOID)AuctionRestriction, sizeof(char), dwProtect, &dwProtect);
				}
			}

			if (ItemCapAddr) {
				if (*ItemCapAddr != bRaiseItemCap ? 10000 : 100) {
					DWORD dwProtect;
					VirtualProtect((LPVOID)ItemCapAddr, sizeof(int), PAGE_EXECUTE_READWRITE, &dwProtect);
					*ItemCapAddr = bRaiseItemCap ? 10000 : 100;
					VirtualProtect((LPVOID)ItemCapAddr, sizeof(int), dwProtect, &dwProtect);
				}
			}

			auto UIEngineInterfaceImpl = oGetUIEngineInterfaceImplInstance();
			if (UIEngineInterfaceImpl) {
				bool* bUseWindowClip = reinterpret_cast<bool*>(UIEngineInterfaceImpl + oUseWindowClipboard);
				*bUseWindowClip = bUseWindowClipboard;
			}

			if (*BNSClientInstance) {
				if (!BNSInstance)
					BNSInstance = *(BNSClient**)BNSClientInstance;

				if (*BNSInstance->GameWorld && oAddInstantNotification) {
					oAddInstantNotification(
						BNSInstance->GameWorld, // Instance Ptr (Should be Game World Ptr)
						xorstr_(L"<image path2=\"00010047.UICommand_Character_Info\" u2=\"0.0\" v2=\"0.0\" ul2=\"64.0\" vl2=\"64.0\" width2=\"64.0\" height2=\"64.0\" imagesetpath3=\"00010047.ItemIcon.Background_Grade_4\" imagesetpath=\"00015590.TextSlot_Item\" enablescale=\"true\" scalerate=\"3.00\"/><font name=\"00008130.Program.Fontset_ItemGrade_4\"> Multi-Tool QoL Reloaded</font>"), // Msg
						xorstr_(L"/Game/Art/FX/01_Source/05_SF/FXUI_03/Particle/UI_CommonFX_Bosskill.UI_CommonFX_Bosskill"), // Particle Ref
						xorstr_(L"00003805.Signal_UI.S_Sys_Control_Mode_Change_BnS"), // Sound
						0, // Track
						true, // Stop Previous Sound
						false, // Headline2
						false, // Boss Headline
						false, // Chat
						0, // Other Category type if none of the above (0 = Scrolling Text headline)
						L"" // Sound 2
					);
				}
			}
		}
		else if (ReloadInProgress && InputKeyEvent->KeyState == EngineKeyStateType::EKS_RELEASED)
			ReloadInProgress = false;
	}

	return oBInputKey(thisptr, InputKeyEvent);
}

// Using a DllLoadedNotification to tell us when we can enable clipboard.
// Initially when the game first initializes this is true but gets flipped off once the main engine interface gets fully initialized.
// Usually when WLAN API is loaded to connect to the server is when it's safest to flip this back on. Also initializing UEngine object as it is now available
// I could just get rid of the op that flips it off but I was initially making this standalone for KR but got side tracked
// I should make better use of this in general as this is a good time to get the pointers for various objects that will stay static like BNSClient, GEngine etc
void NTAPI DllNotification(ULONG notification_reason, const LDR_DLL_NOTIFICATION_DATA* notification_data, PVOID context)
{
	if (notification_reason == LDR_DLL_NOTIFICATION_REASON_LOADED) {
		if (wcsncmp(notification_data->Loaded.BaseDllName->Buffer, xorstr_(L"wlanapi"), 7) == 0) {
			auto UIEngineInterfaceImpl = oGetUIEngineInterfaceImplInstance();

			if (UIEngineInterfaceImpl) {
				bool* bUseWindowClip = reinterpret_cast<bool*>(UIEngineInterfaceImpl + oUseWindowClipboard);
				*bUseWindowClip = bUseWindowClipboard;

				// Another way to do it is by calling the virtual function that handles changing the variable the issue is the vtable can shift in the future
				// Yes GetUIEngineInterfaceImplInstance() is the same as UIEngineInterfaceGetInstance()
				//auto instance = (IUIEngineInterface*)UIEngineInterfaceImpl;
				//instance->vfptr->SetEnableWindowClipBoard(instance, true);
			}
		}
	}
	return;
}

void __cdecl oep_notify([[maybe_unused]] const Version client_version)
{
	if (const auto module = pe::get_module()) {

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
			MessageBox(NULL, xorstr_(L"Failed to load multitool_qol xml"), xorstr_(L"Load Failure"), MB_OK);
			return;
		}

		/*
			Retrieve options via multitool_qol.xml in Documents\BnS
			This section is not used if it fails to load the xml
		*/
		DWORD oldprotect;
		bAutoCombatEverywhere = (CfgDoc.select_node(xorstr_("/config/options/option[@name='AutoCombat']")).node().attribute(xorstr_("enable")).as_bool());
		bAutoBait = (CfgDoc.select_node(xorstr_("/config/options/option[@name='useAutoBait']")).node().attribute(xorstr_("enable")).as_bool());
		bRaiseItemCap = (CfgDoc.select_node(xorstr_("/config/options/option[@name='useItemCap']")).node().attribute(xorstr_("enable")).as_bool());
		bAuctionEverywhere = (CfgDoc.select_node(xorstr_("/config/options/option[@name='useMarketplace']")).node().attribute(xorstr_("enable")).as_bool());
		bAutoCombatRange = (CfgDoc.select_node(xorstr_("/config/options/option[@name='AutoCombat']")).node().attribute(xorstr_("useRange")).as_bool());
		bNoCameraLock = (CfgDoc.select_node(xorstr_("/config/options/option[@name='useNoCameraLock']")).node().attribute(xorstr_("enable")).as_bool());
		bNoWallRunStamina = (CfgDoc.select_node(xorstr_("/config/options/option[@name='useNoWallRunStamina']")).node().attribute(xorstr_("enable")).as_bool());
		useDebug = (CfgDoc.select_node(xorstr_("/config/options/option[@name='useDebug']")).node().attribute(xorstr_("enable")).as_bool());
		bUseCustomGCD = (CfgDoc.select_node(xorstr_("/config/gcd")).node().attribute(xorstr_("enable")).as_bool());
		bIgnorePing = (GCDPing)(CfgDoc.select_node(xorstr_("/config/gcd")).node().attribute(xorstr_("ignorePing")).as_int());
		bAutoModeTurnOffOnDeath = (CfgDoc.select_node(xorstr_("/config/options/option[@name='AutoCombat']")).node().attribute(xorstr_("TurnOffOnDeath")).as_bool());
		bUseWindowClipboard = CfgDoc.select_node(xorstr_("/config/options/option[@name='useWindowClipboard']")).node().attribute(xorstr_("enable")).as_bool();
		auto license_key = CfgDoc.select_node(xorstr_("/config/options/option[@name='license_key']")).node().attribute(xorstr_("key")).as_string();

		if (bAutoCombatRange)
			vAutoCombat_Range = (CfgDoc.select_node(xorstr_("/config/options/option[@name='AutoCombat']")).node().attribute(xorstr_("range")).as_int()) * static_cast<float>(100) / 2;

		if (useDebug)
			AllocConsole();

		auto sBinput = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("0F B6 47 18 48 8D 4C 24 30 89 03")));
		if (sBinput != data.end()) {
			uintptr_t aBinput = (uintptr_t)&sBinput[0] - 0x38;
			oBInputKey = module->rva_to<std::remove_pointer_t<decltype(oBInputKey)>>(aBinput - handle);
			DetourAttach(&(PVOID&)oBInputKey, &hkBInputKey);
		}

		auto sAutoModeResurrection = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("40 53 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 48 8B DA 48 85 C0 74 15")));
		if (sAutoModeResurrection != data.end()) {
			oBnsResurrectionTickTask = module->rva_to<std::remove_pointer_t<decltype(oBnsResurrectionTickTask)>>((uintptr_t)&sAutoModeResurrection[0] - handle);
			DetourAttach(&(PVOID&)oBnsResurrectionTickTask, &hkBnsresurrectionTickTask);
		}
		else if (useDebug)
			CLog(xorstr_("Failed to hook BnsAutoModeResurrection::Tick()"));
		else
			MessageBox(NULL, xorstr_(L"Could not find the function AutoModeResurrection, Feature not enabled"), xorstr_(L"Search Error"), MB_OK);

		/*
			Auto Combat enablation
			Hook the function that checks the zone rule for if Auto Combat is allowed in this zone.
		*/
		CLog(xorstr_("Searching for AutoCombatZoneRuleCheck"));
		auto sAutoCombat = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("41 FF 90 08 01 00 00 90 40 0F B6 C6 48 8B 5C 24 60 48 8B 6C 24 68 48 8B 74 24 70 48 83 C4 40 41 5F")));
		uintptr_t aAutoCombat = NULL;
		if (sAutoCombat != data.end()) {
			CLog(xorstr_("Hooking AutoCombatZoneRuleCheck"));
			aAutoCombat = (uintptr_t)&sAutoCombat[0] - 0x203;
			oCheckAutoCombatZone = module->rva_to<std::remove_pointer_t<decltype(oCheckAutoCombatZone)>>(aAutoCombat - handle);
			DetourAttach(&(PVOID&)oCheckAutoCombatZone, &hkCheckAutoCombatZone);
		}
		else if (useDebug)
			CLog(xorstr_("Failed to hook AutoCombatZoneRuleCheck"));
		else
			MessageBox(NULL, xorstr_(L"Could not find the function for auto combat feature not enabled"), xorstr_(L"Search Error"), MB_OK);

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
		CLog(xorstr_("Searching for AutoCombatRange"));
		auto sAutoCombatRange = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("41 0F B6 4E 3C 4C 89 65 80 88 4D 80 88 45 81")));
		uintptr_t aAutoCombatRange = NULL;
		if (sAutoCombatRange != data.end()) {
			CLog(xorstr_("Hooking AutoCombatRange"));
			aAutoCombatRange = (uintptr_t)&sAutoCombatRange[0] - 0xF5;
			oUBTService_BnsFindTargetTickNode = module->rva_to<std::remove_pointer_t<decltype(oUBTService_BnsFindTargetTickNode)>>(aAutoCombatRange - handle);
			DetourAttach(&(PVOID&)oUBTService_BnsFindTargetTickNode, &hkUBTService_BnsFindTargetTickNode);
		}
		else if (useDebug)
			CLog(xorstr_("Failed to AutoCombatRange"));
		else
			MessageBox(NULL, xorstr_(L"Could not find the function for auto combat range feature not enabled"), xorstr_(L"Search Error"), MB_OK);

		/*
			Auto Bait enablation
		*/
		// Hook the function responsible for parsing what type of bait the fishing paste is.
		// 48 8B 88 E4 37 00 00 48 8B 01 FF 90 B8 00 00 00 48 8B D8 48 89 44 24 38 BD FF FF FF FF 8B F5 89 6C 24 40 old pattern but adjusted to cover TW
		CLog(xorstr_("Searching for IsAutomaticFishingPasteItem"));
		auto sAutoBait = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("48 8B 05 ?? ?? ?? ?? 48 8B 88 ?? ?? ?? ?? 48 8B 01 FF 90 B8 00 00 00 48 8B D8 48 89 44 24 38 BD FF FF FF FF 8B F5 89 6C 24 40 48 8B 05 ?? ?? ?? ?? 48 85 C0 74 26 48 8B 08 48 85 C9 74 1E 48 85 DB 74")));
		uintptr_t aAutoBait = NULL;
		if (sAutoBait != data.end()) {
			CLog(xorstr_("Hooking IsAutomaticFishingPasteItem"));
			aAutoBait = (uintptr_t)&sAutoBait[0] - 0x1E;
			oIsAutomaticFishingPasteItem = module->rva_to<std::remove_pointer_t<decltype(oIsAutomaticFishingPasteItem)>>(aAutoBait - handle);
			DetourAttach(&(PVOID&)oIsAutomaticFishingPasteItem, &hkIsAutomaticFishingPasteItem);
		}
		else if (useDebug)
			CLog(xorstr_("Failed to hook FishPasteType"));
		else
			MessageBox(NULL, xorstr_(L"Could not find the function for fish baits feature not enabled"), xorstr_(L"Search Error"), MB_OK);

		/*
			Raise Item Cap
			Searches for the function responsible for setting the received-items limit
			It is a hard-coded value within the function setting it to 100, we adjust the bytes
			to say the limit is 10,000
		*/
		CLog(xorstr_("Searching for MaxItem: 100"));
		auto sItemCap = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("41 BE 64 00 00 00 41 3B C6")));
		if (sItemCap != data.end()) {
			CLog(xorstr_("Raising MaxItem: 10,000"));
			ItemCapAddr = reinterpret_cast<int*>(&sItemCap[0] + 0x2);
			memcpy((LPVOID)ItemCap_Original, (LPVOID)ItemCapAddr, sizeof(ItemCap_Original));
			if (bRaiseItemCap) {
				VirtualProtect((LPVOID)ItemCapAddr, sizeof(int), PAGE_EXECUTE_READWRITE, &oldprotect);
				*ItemCapAddr = 10000;
				VirtualProtect((LPVOID)ItemCapAddr, sizeof(int), oldprotect, &oldprotect);
			}
		}
		else if (useDebug)
			CLog(xorstr_("Failed to find MaxItem: 100"));
		else
			MessageBox(NULL, xorstr_(L"Could not find the function for mailbox items feature not enabled"), xorstr_(L"Search Error"), MB_OK);

		/*
			Marketplace in cross-server
		*/
		CLog(xorstr_("Searching for Marketplace Restriction"));
		auto sMarketplace = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("4C 8B 0D ?? ?? ?? ?? 4D 85 C9 74 ?? 41 80 79 70 01")));
		uintptr_t aMarketplace = NULL;
		if (sMarketplace != data.end()) {
			CLog(xorstr_("Removing Marketplace Restriction"));
			AuctionRestriction = (uintptr_t)&sMarketplace[0] + 0xA;
			if (bAuctionEverywhere) {
				VirtualProtect((LPVOID)AuctionRestriction, sizeof(char), PAGE_EXECUTE_READWRITE, &oldprotect);
				*reinterpret_cast<BYTE*>(AuctionRestriction) = 0xEB;
				VirtualProtect((LPVOID)AuctionRestriction, sizeof(AuctionRestriction), oldprotect, &oldprotect);
			}
		}
		else if (useDebug)
			CLog(xorstr_("Failed to remove Marketplace Restriction"));
		else
			MessageBox(NULL, xorstr_(L"Could not find the function for marketplace feature not enabled"), xorstr_(L"Search Error"), MB_OK);

		// Hook the function responsible for hnadling the locking state of a players camera
		auto sCameraLock = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("32 C0 48 83 C4 20 5B C3 CC 48 83")));
		CLog(xorstr_("Searching for CameraLock"));
		uintptr_t aCameraLock = NULL;
		if (sCameraLock != data.end()) {
			CLog(xorstr_("Jumping CameraLock"));
			CameraLockCondition = (uintptr_t)&sCameraLock[0] + 0x32;
			if (bNoCameraLock) {
				VirtualProtect((LPVOID)CameraLockCondition, sizeof(char), PAGE_EXECUTE_READWRITE, &oldprotect);
				*reinterpret_cast<BYTE*>(CameraLockCondition) = 0xEB;
				VirtualProtect((LPVOID)CameraLockCondition, sizeof(char), oldprotect, &oldprotect);
			}
		}
		else if (useDebug)
			CLog(xorstr_("Failed to find CameraLock"));
		else
			MessageBox(NULL, xorstr_(L"Could not find the function for camera locking feature not enabled"), xorstr_(L"Search Error"), MB_OK);

		CLog(xorstr_("Searching for StaminaConsumption"));
		/*
			A function called when the player is 'sprinting' and handles the stamina consumption
			We hook this to accurately get the pointer to the stamina structure.
		*/
		auto sStaminaConsumption = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("48 C7 45 A0 FE FF FF FF 48 89 58 10 48 89 70 18 48 89 78 20 0F 29 70 E8 0F 29 78 D8")));
		uintptr_t aStaminaConsumption = NULL;
		if (sStaminaConsumption != data.end()) {
			CLog(xorstr_("Hooking StaminaConsumption"));
			HyperMoveOffset = *reinterpret_cast<int*>((uintptr_t)&sStaminaConsumption[0] + 0x36);
			aStaminaConsumption = (uintptr_t)&sStaminaConsumption[0] - 0x12;
			oStaminMonitor = module->rva_to<std::remove_pointer_t<decltype(oStaminMonitor)>>(aStaminaConsumption - handle);
			DetourAttach(&(PVOID&)oStaminMonitor, &hkStaminaMonitor);
		}
		else if (useDebug)
			CLog(xorstr_("Failed to hook StaminaConsumption"));
		else
			MessageBox(NULL, xorstr_(L"Could not find function for Stamina feature not enabled"), xorstr_(L"Search Error"), MB_OK);

		CLog(xorstr_("Searching for SetGlobalCoolTime_Renewal"));
		// One of the final end-points where a skills GCD value is parsed, offset by delay between server and other things.
		auto sGCDHook = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("48 89 5C 24 10 48 89 74 24 20 57 48 81 EC 90 00 00 00 8B 01 41 8B D8 0F 29 B4 24 80 00 00 00")));
		uintptr_t aGCDHook = NULL;
		if (sGCDHook != data.end()) {
			CLog(xorstr_("Hooking SetGlobalCoolTime_Renewal"));
			aGCDHook = (uintptr_t)&sGCDHook[0];
			oSetGlobalCoolTime_renewal = module->rva_to<std::remove_pointer_t<decltype(oSetGlobalCoolTime_renewal)>>(aGCDHook - handle);
			DetourAttach(&(PVOID&)oSetGlobalCoolTime_renewal, &hkSetGlobalCoolTime_renewal);
		}
		else if (useDebug)
			CLog(xorstr_("Failed to hook SetGlobalCoolTime_Renewal"));

		CLog(xorstr_("Searching for SetSkillRecycleTime_renewal"));
		// One of the final end-points where a skills CD value is parsed, offset by delay between server and other things.
		auto sSCDHook = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("48 89 5C 24 08 48 89 74 24 10 57 48 81 EC 90 00 00 00 49 63 D8")));
		uintptr_t aSCDHook = NULL;
		if (sSCDHook != data.end()) {
			CLog(xorstr_("Hooking SetSkillRecycleTime_renewal"));
			aSCDHook = (uintptr_t)&sSCDHook[0];
			oSetSkillRecycleTime_renewal = module->rva_to<std::remove_pointer_t<decltype(oSetSkillRecycleTime_renewal)>>(aSCDHook - handle);
			DetourAttach(&(PVOID&)oSetSkillRecycleTime_renewal, &hkSetSkillRecycleTime_renewal);
		}
		else if (useDebug)
			CLog(xorstr_("Failed to hook SetSkillRecycleTime_renewal"));
		else
			MessageBox(NULL, xorstr_(L"Could not find function for GCD feature not enabled"), xorstr_(L"Search Error"), MB_OK);

		auto sClip = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("48 8B 01 45 33 C0 BA ?? 14 00 00 FF 50 10 48 8B D8 48 89 44 24 40")));
		if (sClip != data.end()) {
			oGetUIEngineInterfaceImplInstance = module->rva_to<std::remove_pointer_t<decltype(oGetUIEngineInterfaceImplInstance)>>((uintptr_t)&sClip[0] - 0x37 - handle);
			memcpy(&oUseWindowClipboard, &sClip[0] + 0x3F, 4);
		}

		// This pointer is the same as BShowHud function below, either way think of it as redundency if one of these patterns fail
		auto sLatency = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("48 89 5C 24 08 57 48 83 EC 30 48 8B 05 ?? ?? ?? ?? 48 8B FA 48 8B 58 48 48 85 DB")));
		if (sLatency != data.end()) {
			nNet = GetAddress((uintptr_t)&sLatency[0] + 0xA, 3, 7);
		}

		// Used for sending notifications about certain actions
		auto sAddNotif = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("45 33 DB 41 8D 42 ?? 3C 02 BB 05 00 00 00 41 0F 47 DB")));
		if (sAddNotif != data.end()) {
			oAddInstantNotification = module->rva_to<std::remove_pointer_t<decltype(oAddInstantNotification)>>((uintptr_t)&sAddNotif[0] - 0x68 - handle);
		}

		auto sBShowHud = std::search(data.begin(), data.end(), pattern_searcher(xorstr_("45 32 D2 32 DB 48 89 6C 24 38 44 8B CE 85")));
		if (sBShowHud != data.end()) {
			BNSClientInstance = (uintptr_t*)GetAddress((uintptr_t)&sBShowHud[0] + 0x5A, 3, 7);
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
	static PVOID cookie;
	if (tLdrRegisterDllNotification LdrRegisterDllNotification = reinterpret_cast<tLdrRegisterDllNotification>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "LdrRegisterDllNotification")))
		LdrRegisterDllNotification(0, DllNotification, NULL, &cookie); //Set a callback for when Dll's are loaded/unloaded
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