// OSequencer.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include <CommDlg.h>
#include "OSequencer.h"
#define	MLINTERFACE	3 //I want to use latest interface to interact with Mathematica 7 and later.
#include "mathlink.h"
#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;								// current instance
TCHAR szTitle[MAX_LOADSTRING] = "OSequencerConsole";					// The title bar text
TCHAR szWindowClass[MAX_LOADSTRING] = "OSequencerWin";			// the main window class name
char				g_szErrorMsg[1024];
// Forward declarations of functions included in this code module:
ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK	About(HWND, UINT, WPARAM, LPARAM);
HANDLE hMutex  = NULL;
MLENV g_MLEvn = NULL;
#define STR_UNIQUE_LINKNAME	"OLabSeq"
MLINK				g_mlExist = NULL;
MLINK				g_mlLinkOne = NULL;
MLINK				g_mlLinkTwo = NULL;
int					g_nLinkIndex = 0;
#define EXIST_LINK_ARGC		7
MLINK	OpenExistingLink(int argcIn, char** argvIn)
{
	char FAR*	argv[EXIST_LINK_ARGC];
	argv[0] = "-linkmode";
	argv[1] = "connect";
	argv[2] = "-linkname";
	argv[3] = STR_UNIQUE_LINKNAME;
	argv[4] = "-linkprotocol";
	argv[5] = "filemap";
	argv[6] = 0;
	int nErr = 0;
	g_mlExist = MLOpenArgv(g_MLEvn, argv, &argv[6], &nErr);
	if ( argcIn )
	{
		if ( g_mlExist )
		{
			int nCount = argcIn;
			MLPutFunction(g_mlExist, "List", argcIn);
			if ( argcIn > 0 )
			{
				do {
					MLPutString(g_mlExist, *argvIn);
					argvIn++;
					nCount--;
				}
				while(nCount);
			}
		}
		ReleaseMutex(hMutex);
		MLClose(g_mlExist);
	}
	return g_mlExist;
}
MLINK	OpenNewLink(int argcIn, char** argvIn)
{
	char FAR*	argv[7];
	argv[0] = "-linkmode";
	argv[1] = "listen";
	argv[2] = "-linkname";
	argv[3] = STR_UNIQUE_LINKNAME;
	argv[4] = "-linkprotocol";
	argv[5] = "filemap";
	argv[6] = 0;
	int nErr = 0;
	g_mlExist = MLOpenArgv(g_MLEvn, argv, &argv[6], &nErr);
	const char* pp = MLErrorString(g_MLEvn, nErr);
	ReleaseMutex(hMutex);
	return g_mlExist;
}
#define MAX_LINK_CACHE	256
MLINK	g_mlCache[MAX_LINK_CACHE];
int		CloseLink(MLINK ml)
{
	int iCache = 0, nRet = 0;
	do 
	{
		if ( g_mlCache[iCache] == ml )
		{
			MLClose(ml);
			g_mlCache[iCache] = NULL;
			nRet = 1;
		}
		iCache++;
	} while (iCache < MAX_LINK_CACHE);

	return nRet;
}
MLINK		HandleLink()
{
	if ( g_mlExist || OpenNewLink(0, NULL) )
	{
		int nError = MLError(g_mlExist);
		if ( nError == 0 || nError == 10 || OpenNewLink(0, NULL) )
		{
			int nReady = MLReady(g_mlExist);
			if ( nReady )
			{
				WaitForSingleObject(hMutex, -1);
				long_st lst;
				char*	sz[512];
				int		len[512];
				if ( MLCheckFunction(g_mlExist, "List", &lst) )
				{
					for ( int ii = 0; ii < lst; ii++ )
						MLGetByteString(g_mlExist, (const unsigned char**)&sz[ii], &len[ii], 255);
				}
				MLINK ml = MLOpenArgv(g_MLEvn, sz, &sz[lst], 0);
				for ( int jj = 0; jj < lst; jj++ )
					MLDisownByteString(g_mlExist, (const unsigned char*)sz[jj], len[jj]);
				MLClose(g_mlExist);
				g_mlExist = NULL;
				return ml;
			}
		}
	}
	return NULL;
}
 void	far pascal  DefaultMessageHandler(MLINK mlp, int m, int n)
{
	if ( mlp == g_mlLinkTwo && g_mlLinkOne )
	{
		MLPutMessage(g_mlLinkOne, m);
	}
	else if ( mlp == g_mlLinkOne && g_mlLinkTwo )
	{
		if ( m != 1 )
			MLPutMessage(g_mlLinkTwo, m);
	}
}
int	GetLinkCache(MLINK ml)
{
	int ii = 0;
	for ( ii = 0; ii < MAX_LINK_CACHE; ii++ )
	{
		if ( !g_mlCache[ii] ) //find empty slot
			break;
	}
	if ( ii >= MAX_LINK_CACHE ) //cache buffer is full.
		return -1;
	g_mlCache[ii] = ml;
	return ii;
}
int	HasLinkCache()
{
	for ( int ii = 0; ii < MAX_LINK_CACHE; ii++ )
	{
		if ( g_mlCache[ii] )
			return 1;
	}
	return 0;
}
int	g_nTransferFlag = 0;
int	TransferPacket(MLINK pLink, MLINK ml)
{
	MLINKMark nLink = MLCreateMark(pLink);
	mlapi_packet pk = MLNextPacket(pLink);
	if ( !pk )
	{
		MLClearError(pLink);
	}
	MLSeekToMark(pLink, nLink, 0);
	MLDestroyMark(pLink, nLink);
	int nRet = MLTransferExpression(ml, pLink);
	if ( pk == 15 || pk == 14 || pk == 13 )
		g_nTransferFlag = 1;
	return nRet;
}
int	TransferPacketRev(MLINK pLink, MLINK ml)
{
	MLINKMark nLink = MLCreateMark(ml);
	mlapi_packet pk = MLNextPacket(ml);
	if ( !pk )
	{
		MLClearError(ml);
	}
	MLSeekToMark(ml, nLink, 0);
	int nRet = MLTransferExpression(pLink, ml);
	MLDestroyMark(ml, nLink);
	if ( pk == 8 )
		g_nTransferFlag = 0;
	return nRet;
}
#define KERNEL_COMMAND_LENGTH	2048
#define DEFAULT_MATHKERNEL_NAME	"MathKernel"
#define DEFAULT_CONFIG_FILE		"OMathLinkConfig.ini"
char	szKernelCommandLine[KERNEL_COMMAND_LENGTH];
#include <cstdio>
static	LPCSTR	_get_ini_file()
{
	static	char	szIniFile[KERNEL_COMMAND_LENGTH];
	static	bool	bInited = false;
	if ( !bInited )
	{
		char	szPath[KERNEL_COMMAND_LENGTH];
		GetTempPathA(KERNEL_COMMAND_LENGTH, szPath);
		sprintf_s(szIniFile, KERNEL_COMMAND_LENGTH, "%s%s", szPath, DEFAULT_CONFIG_FILE);
		bInited = true;
	}
	return szIniFile;
}
#define CONFIG_FILENAME	_get_ini_file()
static	int	_init_command_line(char* lpCommandLineBuffer, int nBufferSize)
{
#define MAX_KERNELNAME_LENGTH	1024
	char	szKernelName[MAX_KERNELNAME_LENGTH];
	GetPrivateProfileStringA("MathKernelExecutable", "Location", DEFAULT_MATHKERNEL_NAME, szKernelName, MAX_KERNELNAME_LENGTH, CONFIG_FILENAME);
	if ( lstrcmpiA(szKernelName, DEFAULT_MATHKERNEL_NAME) == 0 ) //default, not configured, need to ask user for the mathkernel.exe position
	{
		OPENFILENAMEA ofn = {0};
		ofn.lStructSize = sizeof(OPENFILENAMEA);
		ofn.hwndOwner = NULL;
		ofn.lpstrFile = szKernelName;
		ofn.nMaxFile = MAX_KERNELNAME_LENGTH;
		ofn.lpstrFilter = "Executable Files(*.exe)\0*.exe\0\0";
		ofn.Flags = OFN_EXPLORER;
		ofn.lpstrTitle = "Choose a MathLink program to launch";
		ofn.nFilterIndex = 1;
		if ( GetOpenFileNameA(&ofn) )
		{
			WritePrivateProfileStringA("MathKernelExecutable", "Location", szKernelName, CONFIG_FILENAME);
		}
	}
	sprintf_s(lpCommandLineBuffer, nBufferSize, "-linkmode launch -linkname \'%s -mathlink\'", szKernelName);
	return 0;
}
int	RunLoop(HINSTANCE hInstance, int argc, char** argv)
{
	MLEnvironment envLocal = MLInitialize(NULL);
	MLINK mlLinkCheck = NULL;
	MLINK mlCacheTwo = NULL;
	if ( envLocal )
	{
		MLMessageHandlerObject msgHandler = MLCreateMessageHandler(envLocal, DefaultMessageHandler, 0);

		MLINK pLink = MLOpenArgv(envLocal, argv, argv + sizeof(char*) * argc, NULL);
		g_mlLinkOne = pLink;
		if ( pLink )
		{
			if ( GetLinkCache(pLink) >= 0 )
			{
				g_nLinkIndex = 0;
				MLConnect(g_mlLinkOne);
				if ( msgHandler )
					MLSetMessageHandler(g_mlLinkOne, msgHandler);
				Sleep(1000);

				//MLINK ml = MLOpenString(envLocal, "-linkmode launch -linkname \'MathKernel -mathlink\'", 0);
				_init_command_line(szKernelCommandLine, KERNEL_COMMAND_LENGTH);
				MLINK ml = MLOpenString(envLocal, szKernelCommandLine, 0);
				GetCurrentDirectoryA(KERNEL_COMMAND_LENGTH, szKernelCommandLine);
				g_mlLinkTwo = ml;
				if ( ml )
				{
					MLConnect(ml);
					if ( msgHandler )
						MLSetMessageHandler(g_mlLinkTwo, msgHandler);

					if ( g_mlLinkTwo ) //this should be always true, isn't it...
					{
						while ( HasLinkCache() > 0 )
						{
							while ( TRUE )
							{
								int mlErr = 0;
								int mlErrEx = 0;
								int mlReady = MLReady(g_mlLinkOne);
								if ( mlReady )
								{
									mlErr = MLError(g_mlLinkOne);
									if ( !mlErr )
									{
										TransferPacket(g_mlLinkOne, g_mlLinkTwo);
									}
								}
								int mlReadyEx = MLReady(g_mlLinkTwo);
								if ( mlReadyEx )
								{
									mlErrEx = MLError(g_mlLinkTwo);
									if ( !mlErrEx) 
									{
										TransferPacketRev(g_mlLinkOne, g_mlLinkTwo);
									}
								}
								if ( mlErr || mlErrEx )
									break;
								if ( !mlReady && !mlReadyEx )
								{
									MLFlush(g_mlLinkOne);
									if ( !MLReady(g_mlLinkOne) )
									{
										MLFlush(g_mlLinkTwo);
										if ( !MLReady(g_mlLinkTwo) )
											break;
									}
								}
							}//end of while ( TRUE )
							if ( !g_mlLinkOne || !g_nTransferFlag && !MLReady(g_mlLinkOne) )
							{
								int nIndex = 0, iCache = 0;
								while ( !g_mlCache[iCache] || !MLReady(g_mlCache[iCache]) )
								{
									iCache++;
									nIndex++;
									if ( iCache >= MAX_LINK_CACHE )
										goto LABEL_EXCEED;
								}
								g_mlLinkOne = g_mlCache[iCache];
								g_nLinkIndex = iCache;
							}
LABEL_EXCEED:
							if ( g_mlLinkOne )
							{
								int nErr = MLError(g_mlLinkOne);
								if ( nErr == MLECLOSED || nErr == MLEDEAD )
								{
									CloseLink(g_mlLinkOne);
									g_mlLinkOne = NULL;
									g_nLinkIndex = 0;
								}
							}
							int nErr = MLError(g_mlLinkTwo);
							if ( nErr == MLECLOSED || nErr == MLEDEAD )
							{
								MLClose(g_mlLinkTwo);
								g_mlLinkTwo = NULL;
							}
							MLINK mlRet = HandleLink();
							if ( mlRet )
							{
								do 
								{
									int nIndex = GetLinkCache(mlRet);
									if ( msgHandler )
										MLSetMessageHandler(mlRet, msgHandler);
									if( nIndex < 0 )
										break;
									MLPutFunction(mlRet, "InputNamePacket", 1);
									MLPutString(mlRet, "In[1]:= ");
									MLEndPacket(mlRet);
									MLFlush(mlRet);

									mlRet = HandleLink();
			
								} while (mlRet);
							}
							MSG msg;
							while ( PeekMessage(&msg, NULL, 0, 0, TRUE) )
							{
								TranslateMessage(&msg);
								DispatchMessage(&msg);
							}
							Sleep(10);
							mlCacheTwo = g_mlLinkTwo;
							if ( !g_mlLinkTwo )
								goto LABEL_CLOSE;
						}
						mlCacheTwo = g_mlLinkTwo;
LABEL_CLOSE:
						if ( mlCacheTwo )
							MLClose(mlCacheTwo);
					}
					//v10 == v33 ?
				}
			}
			//v29
			for ( int iCache = 0; iCache < MAX_LINK_CACHE; iCache++ )
			{
				if ( g_mlCache[iCache] )
				{
					CloseLink(g_mlCache[iCache]);
				}
			}
		}
		MLDeinitialize(envLocal);
	}
	return 0;
}
//int	__stdcall	OSequencer_MessageHandler(int )
int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPTSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

 	// TODO: Place code here.
	char		buff[512];
	char FAR*	buff_start = buff;
	char FAR*	argv[32];
	char FAR**	argv_end = argv + 32;
#ifdef _DEBUG
	MessageBox(NULL, "Sophy stops the process from executing, independent proxy", "Waiting for you", MB_OK); //use this to stop/attach/debug
#endif // _DEBUG
	g_MLEvn = MLInitialize(NULL);
		MLScanString(argv, &argv_end, &lpCmdLine, &buff_start);
	int nArgCount = (int)(argv_end - argv); //number of arguments.
	hMutex = CreateMutex(NULL, TRUE, "SeqOLab");
	if ( hMutex )
	{
		MLINK mlink = NULL;
		if ( GetLastError() == ERROR_ALREADY_EXISTS )
		{
			mlink = OpenExistingLink(nArgCount, argv);
		}
		else
		{
			mlink = OpenNewLink(nArgCount, argv);
			if ( mlink )
			{
				if ( InitInstance(hInstance, nCmdShow) )
				{
					int nRet = RunLoop(hInstance, nArgCount, argv);
					MLDeinitialize(g_MLEvn);
					return nRet;
				}
			}
		}
		return 0;
	}
	return 1;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= WndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= LoadIcon(hInstance, MAKEINTRESOURCE(IDI_OSEQUENCER));
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName	= MAKEINTRESOURCE(IDC_OSEQUENCER);
	wcex.lpszClassName	= szWindowClass;
	wcex.hIconSm		= LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassEx(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//

LRESULT __stdcall OSeqWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	return DefWindowProcA(hWnd, Msg, wParam, lParam);
}
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   HWND hWnd;

   hInst = hInstance; // Store instance handle in our global variable
   WNDCLASSA WndClass;
   WndClass.hInstance = hInstance;
   WndClass.style = 0;
   WndClass.lpfnWndProc = OSeqWndProc;
   WndClass.cbClsExtra = 0;
   WndClass.cbWndExtra = 0;
   WndClass.hIcon = LoadIconA(NULL, (LPCSTR)0x7f00);
   WndClass.hCursor = LoadCursorA(hInstance, (LPCSTR)0x7f00);
   WndClass.hbrBackground = (HBRUSH)GetStockObject(0);
   WndClass.lpszMenuName = NULL;
   WndClass.lpszClassName = szWindowClass;
   RegisterClassA(&WndClass);
   hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);

   if (!hWnd)
   {
      return FALSE;
   }

   //ShowWindow(hWnd, nCmdShow);
   ShowWindow(hWnd, SW_HIDE);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND	- process the application menu
//  WM_PAINT	- Paint the main window
//  WM_DESTROY	- post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	PAINTSTRUCT ps;
	HDC hdc;

	switch (message)
	{
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		// Parse the menu selections:
		switch (wmId)
		{
		case IDM_ABOUT:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
			break;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		// TODO: Add any drawing code here...
		EndPaint(hWnd, &ps);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}
