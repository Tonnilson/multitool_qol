uintptr_t CameraLockCondition = NULL;
int* ItemCapAddr = NULL;
uintptr_t AuctionRestriction = NULL; // 0x74

#define SLODWORD(x)  (*((int*)&(x)))


BYTE ItemCap_Original[] = { 0x41, 0xBE, 0x10, 0x27, 0x00, 0x00 };
BYTE ItemCap_Raised[] = { 0x41, 0xBE, 0x10, 0x27, 0x00, 0x00 };

#define IS_KEY_DOWN(key) ((GetAsyncKeyState(key) & (1 << 16)))
#define IN_RANGE(low, high, x) ((low <= x && x <= high))
#define IS_NUMERIC(string) (!string.empty() && std::find_if(string.begin(), string.end(), [](unsigned char c) { return !std::isdigit(c); }) == string.end())

uintptr_t GetAddress(uintptr_t AddressOfCall, int index, int length)
{
	if (!AddressOfCall)
		return 0;

	long delta = *(long*)(AddressOfCall + index);
	return (AddressOfCall + delta + length);
}

class GameSession {
public:
	char pad[0x98];
	float receivedServerPing;
};

// I need to clean up above and merge it all together but too lazy atm
class BNSClient {
public:
	char pad[0x48];
	GameSession* game;
	char pad2[0x50];
	uintptr_t* GameWorld;
	char pad3[0x10];
	uintptr_t* PresentationWorld;
};

class BInputKey {
public:
	int Key;
	bool bCtrlPressed;
	bool bShiftPressed;
	bool bAltPressed;
	bool bDoubleClicked;
	bool bTpsModeKey;
};

enum class GCDPing {
	Enabled = 0,
	GCD_Only = 1,
	CD_Only = 2,
	Disabled = 3
};

enum class EngineKeyStateType {
	EKS_PRESSED = 0,
	EKS_RELEASED = 1,
	EKS_REPEAT = 2,
	EKS_DOUBLECLICK = 3,
	EKS_AXIS = 4
};

class EInputKeyEvent {
public:
	char padding[0x18];
	char _vKey;
	char padd_2[0x2];
	EngineKeyStateType KeyState;
	bool bCtrlPressed;
	bool bShiftPressed;
	bool bAltPressed;
};

struct FWindowsPlatformTime {

};

struct PrecisionTimer {
	unsigned __int64 _startTime;
	FWindowsPlatformTime _timer;
	float _limit;
};
struct IdAndTimer {
	__int64 _id;
};

struct SkillTimer {
	int _coolTimeBias;
	std::map<int, IdAndTimer, std::less<int>, std::allocator<std::pair<int const, IdAndTimer> > > _effectTimeList;
	std::map<int, IdAndTimer, std::less<int>, std::allocator<std::pair<int const, IdAndTimer> > > _recycleTimerList_renewal[7];
	std::map<int, IdAndTimer, std::less<int>, std::allocator<std::pair<int const, IdAndTimer> > > _globalCoolTimeList_renewal;
};

// Notification Function
typedef void(__cdecl* _AddInstantNotification)(
	uintptr_t* thisptr,
	const wchar_t* text,
	const wchar_t* particleRef,
	const wchar_t* sound,
	char track,
	bool stopPreviousSound,
	bool headline2,
	bool boss_headline,
	bool chat,
	char category,
	const wchar_t* sound2);
_AddInstantNotification oAddInstantNotification;

bool(__fastcall* oBInputKey)(BInputKey* thisptr, EInputKeyEvent* InputKeyEvent);

#define LDR_DLL_NOTIFICATION_REASON_LOADED 1
#define LDR_DLL_NOTIFICATION_REASON_UNLOADED 0
typedef NTSTATUS(NTAPI* tLdrRegisterDllNotification)(ULONG, PVOID, PVOID, PVOID);
void NTAPI DllNotification(ULONG notification_reason, const LDR_DLL_NOTIFICATION_DATA* notification_data, PVOID context);

typedef uintptr_t(__cdecl* _GetUIEngineInterfaceImplInstance)();
_GetUIEngineInterfaceImplInstance oGetUIEngineInterfaceImplInstance;

typedef void(__cdecl* _ExecuteConsoleCommandNoHistory)(const wchar_t* szCmd);
_ExecuteConsoleCommandNoHistory ExecuteConsoleCommandNoHistory;

pugi::xml_document CfgDoc;
static std::filesystem::path docPath;
static int useDebug = 0;
static float desiredSpeed = 0.0f;
float vAutoCombat_Range = 1500;
static int HyperMoveOffset = 0;
bool bAutoModeTurnOffOnDeath = false;
int oUseWindowClipboard = 0;

bool bAutoCombatEverywhere = false;
bool bAutoBait = false;
bool bRaiseItemCap = false;
bool bAuctionEverywhere = false;
bool bAutoCombatRange = false;
bool bNoCameraLock = false;
bool bNoWallRunStamina = false;
bool bUseCustomGCD = false;
bool bHongmoonSocks = false;
GCDPing bIgnorePing = GCDPing::Enabled;
bool bUseWindowClipboard = false;

uintptr_t* BNSClientInstance = NULL;
BNSClient* BNSInstance;
uintptr_t nNet = NULL;
