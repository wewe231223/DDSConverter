#include "framework.h"
#include "DDSConverter.h"

#include <memory>

#include <shellapi.h>

#include "Dx12ImguiRenderer.h"


#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#define MAX_LOADSTRING 100

HINSTANCE GlobalInstanceHandle {};
WCHAR GlobalTitle[MAX_LOADSTRING] {};
WCHAR GlobalWindowClass[MAX_LOADSTRING] {};
std::unique_ptr<Dx12ImguiRenderer> GlobalRenderer {};

ATOM MyRegisterClass(HINSTANCE InstanceHandle);
BOOL InitInstance(HINSTANCE InstanceHandle, int CommandShow);
LRESULT CALLBACK WndProc(HWND WindowHandle, UINT Message, WPARAM WParam, LPARAM LParam);
INT_PTR CALLBACK About(HWND DialogHandle, UINT Message, WPARAM WParam, LPARAM LParam);

int APIENTRY wWinMain(_In_ HINSTANCE InstanceHandle, _In_opt_ HINSTANCE PreviousInstanceHandle, _In_ LPWSTR CommandLine, _In_ int CommandShow) {
	UNREFERENCED_PARAMETER(PreviousInstanceHandle);
	UNREFERENCED_PARAMETER(CommandLine);
	LoadStringW(InstanceHandle, IDS_APP_TITLE, GlobalTitle, MAX_LOADSTRING);
	LoadStringW(InstanceHandle, IDC_DDSCONVERTER, GlobalWindowClass, MAX_LOADSTRING);
	MyRegisterClass(InstanceHandle);
	if (!InitInstance(InstanceHandle, CommandShow)) {
		return FALSE;
	}
	HACCEL AcceleratorTable { LoadAccelerators(InstanceHandle, MAKEINTRESOURCE(IDC_DDSCONVERTER)) };
	MSG Message {};
	while (GetMessage(&Message, nullptr, 0, 0)) {
		if (!TranslateAccelerator(Message.hwnd, AcceleratorTable, &Message)) {
			TranslateMessage(&Message);
			DispatchMessage(&Message);
		}
	}
	return static_cast<int>(Message.wParam);
}

ATOM MyRegisterClass(HINSTANCE InstanceHandle) {
	WNDCLASSEXW WindowClassInfo {};
	WindowClassInfo.cbSize = sizeof(WNDCLASSEX);
	WindowClassInfo.style = CS_HREDRAW | CS_VREDRAW;
	WindowClassInfo.lpfnWndProc = WndProc;
	WindowClassInfo.cbClsExtra = 0;
	WindowClassInfo.cbWndExtra = 0;
	WindowClassInfo.hInstance = InstanceHandle;
	WindowClassInfo.hIcon = LoadIcon(InstanceHandle, MAKEINTRESOURCE(IDI_DDSCONVERTER));
	WindowClassInfo.hCursor = LoadCursor(nullptr, IDC_ARROW);
	WindowClassInfo.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	WindowClassInfo.lpszMenuName = MAKEINTRESOURCEW(IDC_DDSCONVERTER);
	WindowClassInfo.lpszClassName = GlobalWindowClass;
	WindowClassInfo.hIconSm = LoadIcon(WindowClassInfo.hInstance, MAKEINTRESOURCE(IDI_SMALL));
	return RegisterClassExW(&WindowClassInfo);
}

BOOL InitInstance(HINSTANCE InstanceHandle, int CommandShow) {
	GlobalInstanceHandle = InstanceHandle;
	HWND WindowHandle { CreateWindowW(GlobalWindowClass, GlobalTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, InstanceHandle, nullptr) };
	if (WindowHandle == nullptr) {
		return FALSE;
	}
	ShowWindow(WindowHandle, CommandShow);
	UpdateWindow(WindowHandle);
	return TRUE;
}

LRESULT CALLBACK WndProc(HWND WindowHandle, UINT Message, WPARAM WParam, LPARAM LParam) {
	if (GlobalRenderer != nullptr) {
		LRESULT HandledResult { GlobalRenderer->HandleWindowMessage(WindowHandle, Message, WParam, LParam) };
		if (HandledResult != 0) {
			return HandledResult;
		}
	}
	switch (Message) {
	case WM_CREATE:
		GlobalRenderer = std::make_unique<Dx12ImguiRenderer>();
		GlobalRenderer->Initialize(WindowHandle);
		DragAcceptFiles(WindowHandle, TRUE);
		return 0;
	case WM_SIZE:
		if (GlobalRenderer != nullptr && WParam != SIZE_MINIMIZED) {
			GlobalRenderer->Resize(LOWORD(LParam), HIWORD(LParam));
		}
		return 0;
	case WM_COMMAND: {
		int MenuIdentifier { LOWORD(WParam) };
		switch (MenuIdentifier) {
		case IDM_ABOUT:
			DialogBox(GlobalInstanceHandle, MAKEINTRESOURCE(IDD_ABOUTBOX), WindowHandle, About);
			return 0;
		case IDM_EXIT:
			DestroyWindow(WindowHandle);
			return 0;
		default:
			return DefWindowProc(WindowHandle, Message, WParam, LParam);
		}
	}
	case WM_DROPFILES: {
		HDROP DropHandle { reinterpret_cast<HDROP>(WParam) };
		WCHAR FilePath[MAX_PATH] {};
		if (DragQueryFile(DropHandle, 0, FilePath, MAX_PATH) > 0 && GlobalRenderer != nullptr) {
			GlobalRenderer->LoadDroppedImage(FilePath);
		}
		DragFinish(DropHandle);
		return 0;
	}
	case WM_PAINT: {
		PAINTSTRUCT PaintStruct {};
		BeginPaint(WindowHandle, &PaintStruct);
		if (GlobalRenderer != nullptr) {
			GlobalRenderer->Render();
		}
		EndPaint(WindowHandle, &PaintStruct);
		InvalidateRect(WindowHandle, nullptr, FALSE);
		return 0;
	}
	case WM_DESTROY:
		DragAcceptFiles(WindowHandle, FALSE);
		if (GlobalRenderer != nullptr) {
			GlobalRenderer->Shutdown();
			GlobalRenderer.reset();
		}
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProc(WindowHandle, Message, WParam, LParam);
	}
}

INT_PTR CALLBACK About(HWND DialogHandle, UINT Message, WPARAM WParam, LPARAM LParam) {
	UNREFERENCED_PARAMETER(LParam);
	switch (Message) {
	case WM_INITDIALOG:
		return static_cast<INT_PTR>(TRUE);
	case WM_COMMAND:
		if (LOWORD(WParam) == IDOK || LOWORD(WParam) == IDCANCEL) {
			EndDialog(DialogHandle, LOWORD(WParam));
			return static_cast<INT_PTR>(TRUE);
		}
		break;
	default:
		break;
	}
	return static_cast<INT_PTR>(FALSE);
}
