struct IUIEngineInterface;

struct IUIEngineInterfaceVtbl
{
	int(__fastcall* Load)(IUIEngineInterface* thisptr, const wchar_t*, const wchar_t*, int, const wchar_t*);
	char gap[0xFA8];
	void(__fastcall* SetEnableWindowClipBoard)(IUIEngineInterface* thisptr, bool);
};

struct IUIEngineInterface
{
	IUIEngineInterfaceVtbl* vfptr;
};