#include <windows.h>
#include <winuser.h>
#include <tchar.h>
#include <stdlib.h>
#include <time.h>
#include <strsafe.h>
#include <process.h>

// 临时定义 SS_WORDBREAK（如果仍未定义） / Temporarily define SS_WORDBREAK (if still undefined)
#ifndef SS_WORDBREAK
#define SS_WORDBREAK 0x00000040L
#endif

// Define control IDs
#define ID_COMBO_DRIVES 1001
#define ID_BUTTON_REFRESH 1002
#define ID_BUTTON_COVER1 1003
#define ID_BUTTON_COVER10 1004
#define ID_EDIT_SIZE 1005
#define ID_BUTTON_GENERATE 1006
#define ID_LABEL_SIZE 1007
#define ID_PROGRESS_TEXT 2001
#define ID_BUTTON_OK 2002
#define ID_BUTTON_CANCEL 2003

// Global variables
HWND hComboDrives, hEditSize;
TCHAR selectedDrive[4] = _T("C:\\");
volatile BOOL g_CancelOperation = FALSE;
HWND g_hProgressDlg = NULL;
long long g_TotalBytesToFill = 0;
long long g_BytesFilled = 0;
int g_CurrentRound = 0;
int g_TotalRounds = 0;

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ProgressDlgProc(HWND, UINT, WPARAM, LPARAM);
void LoadDrives(HWND hWnd);
void PerformOperation(HWND hMainWnd, int times);
BOOL FillAndDeleteOneRound(const TCHAR* drivePath, long long* bytesFilled, long long totalToFill);
void GenerateSpecificFile(const TCHAR* drivePath, long long sizeKB);
void UpdateProgressText();
unsigned __stdcall OperationThread(void* param);

// Structure for operation parameters
struct OperationParams {
    HWND hMainWnd;
    const TCHAR* drivePath;
    int times;
    long long sizeKB;
    BOOL isGenerate;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Register main window class
    WNDCLASS wc = { 0 };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    wc.lpszClassName = _T("DiskFillerClass");

    RegisterClass(&wc);

    // Create main window
    HWND hWnd = CreateWindow(_T("DiskFillerClass"), _T("Disk Filler Tool"),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 500, 300,
        NULL, NULL, hInstance, NULL);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // Main message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (g_hProgressDlg && !IsDialogMessage(g_hProgressDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else if (!g_hProgressDlg) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        // Create combo box for selecting drives
        hComboDrives = CreateWindow(_T("COMBOBOX"), NULL,
            CBS_DROPDOWN | CBS_HASSTRINGS | WS_CHILD | WS_VISIBLE | WS_VSCROLL,
            10, 10, 100, 200, hWnd, (HMENU)ID_COMBO_DRIVES,
            (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL);

        // Create refresh button
        CreateWindow(_T("BUTTON"), _T("Refresh"),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            120, 10, 80, 30, hWnd, (HMENU)ID_BUTTON_REFRESH,
            (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL);

        // Create button to overwrite 1 time
        CreateWindow(_T("BUTTON"), _T("Overwrite 1 Time"),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 50, 130, 30, hWnd, (HMENU)ID_BUTTON_COVER1,
            (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL);

        // Create button to overwrite 10 times
        CreateWindow(_T("BUTTON"), _T("Overwrite 10 Times"),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            150, 50, 130, 30, hWnd, (HMENU)ID_BUTTON_COVER10,
            (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL);

        // Create static text for file size label
        CreateWindow(_T("STATIC"), _T("File Size (KB):"),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 90, 120, 30, hWnd, (HMENU)ID_LABEL_SIZE,
            (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL);

        // Create edit box for file size input
        hEditSize = CreateWindow(_T("EDIT"), NULL,
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
            140, 90, 100, 30, hWnd, (HMENU)ID_EDIT_SIZE,
            (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL);

        // Create button to generate file
        CreateWindow(_T("BUTTON"), _T("Generate File"),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            250, 90, 100, 30, hWnd, (HMENU)ID_BUTTON_GENERATE,
            (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL);

        LoadDrives(hWnd);
        break;
    }
    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        switch (wmId) {
        case ID_BUTTON_REFRESH:
            LoadDrives(hWnd);
            break;
        case ID_COMBO_DRIVES:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                // Update selected drive when user changes combo box selection
                int index = SendMessage(hComboDrives, CB_GETCURSEL, 0, 0);
                SendMessage(hComboDrives, CB_GETLBTEXT, index, (LPARAM)selectedDrive);
            }
            break;
        case ID_BUTTON_COVER1:
            PerformOperation(hWnd, 1);
            break;
        case ID_BUTTON_COVER10:
            PerformOperation(hWnd, 10);
            break;
        case ID_BUTTON_GENERATE: {
            TCHAR sizeText[20];
            GetWindowText(hEditSize, sizeText, 20);
            long long sizeKB = _ttoi64(sizeText);
            if (sizeKB > 0) {
                // Start file generation in a separate thread
                OperationParams* params = new OperationParams;
                params->hMainWnd = hWnd;
                params->drivePath = selectedDrive;
                params->times = 1;
                params->sizeKB = sizeKB;
                params->isGenerate = TRUE;
                _beginthreadex(NULL, 0, OperationThread, params, 0, NULL);
                // Modal loop handled by ProgressDlgProc
                MSG msg;
                while (g_hProgressDlg && GetMessage(&msg, NULL, 0, 0)) {
                    if (!IsDialogMessage(g_hProgressDlg, &msg)) {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    }
                }
            }
            else {
                MessageBox(hWnd, _T("Please enter a valid size!"), _T("Error"), MB_OK);
            }
            break;
        }
        }
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void LoadDrives(HWND hWnd) {
    // Clear and populate combo box with fixed drives
    SendMessage(hComboDrives, CB_RESETCONTENT, 0, 0);
    TCHAR drives[256];
    TCHAR* drivePtr = drives;
    GetLogicalDriveStrings(256, drives);
    while (*drivePtr) {
        if (GetDriveType(drivePtr) == DRIVE_FIXED) {
            SendMessage(hComboDrives, CB_ADDSTRING, 0, (LPARAM)drivePtr);
        }
        drivePtr += _tcslen(drivePtr) + 1;
    }
    SendMessage(hComboDrives, CB_SETCURSEL, 0, 0);
    SendMessage(hComboDrives, CB_GETLBTEXT, 0, (LPARAM)selectedDrive);
}

void PerformOperation(HWND hMainWnd, int times) {
    // Register progress dialog class if not already registered
    WNDCLASS wc = { 0 };
    if (!GetClassInfo((HINSTANCE)GetWindowLongPtr(hMainWnd, GWLP_HINSTANCE), _T("ProgressDlgClass"), &wc)) {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ProgressDlgProc;
        wc.hInstance = (HINSTANCE)GetWindowLongPtr(hMainWnd, GWLP_HINSTANCE);
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        wc.lpszClassName = _T("ProgressDlgClass");
        RegisterClass(&wc);
    }

    // 增加高度至 200 像素 / Increase height to 200 pixels
    g_hProgressDlg = CreateWindowEx(0, _T("ProgressDlgClass"), _T("Progress"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 200,
        hMainWnd, NULL, (HINSTANCE)GetWindowLongPtr(hMainWnd, GWLP_HINSTANCE), NULL);

    if (g_hProgressDlg) {
        // 静态文本高度保持 100，充分利用空间 / Static text height remains 100 to fully utilize space
        CreateWindowEx(0, _T("STATIC"), _T("Executing..."),
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_WORDBREAK,
            10, 10, 280, 100, g_hProgressDlg, (HMENU)ID_PROGRESS_TEXT,
            (HINSTANCE)GetWindowLongPtr(hMainWnd, GWLP_HINSTANCE), NULL);

        // 按钮 Y 坐标调整至 150，确保完全显示 / Button Y coordinate adjusted to 130 to ensure full visibility
        CreateWindowEx(0, _T("BUTTON"), _T("OK"),
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_DISABLED,
            100, 130, 80, 30, g_hProgressDlg, (HMENU)ID_BUTTON_OK,
            (HINSTANCE)GetWindowLongPtr(hMainWnd, GWLP_HINSTANCE), NULL);

        CreateWindowEx(0, _T("BUTTON"), _T("Cancel"),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            190, 130, 80, 30, g_hProgressDlg, (HMENU)ID_BUTTON_CANCEL,
            (HINSTANCE)GetWindowLongPtr(hMainWnd, GWLP_HINSTANCE), NULL);

        ShowWindow(g_hProgressDlg, SW_SHOW);
        UpdateWindow(g_hProgressDlg);
        EnableWindow(hMainWnd, FALSE);

        // Start operation in a separate thread
        OperationParams* params = new OperationParams;
        params->hMainWnd = hMainWnd;
        params->drivePath = selectedDrive;
        params->times = times;
        params->sizeKB = 0;
        params->isGenerate = FALSE;
        _beginthreadex(NULL, 0, OperationThread, params, 0, NULL);
    }
}

LRESULT CALLBACK ProgressDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        // Start timer to update progress text every 500ms
        SetTimer(hDlg, 1, 500, NULL);
        return TRUE;
    case WM_TIMER:
        UpdateProgressText();
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_BUTTON_OK:
            DestroyWindow(hDlg);
            EnableWindow(GetParent(hDlg), TRUE);
            g_hProgressDlg = NULL;
            break;
        case ID_BUTTON_CANCEL:
            g_CancelOperation = TRUE;
            DestroyWindow(hDlg);
            EnableWindow(GetParent(hDlg), TRUE);
            g_hProgressDlg = NULL;
            break;
        }
        break;
    case WM_DESTROY:
        KillTimer(hDlg, 1);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hDlg, message, wParam, lParam);
    }
    return 0;
}

void UpdateProgressText() {
    if (g_hProgressDlg) {
        TCHAR text[256];
        if (g_TotalBytesToFill > 0) {
            double percent = (double)g_BytesFilled / g_TotalBytesToFill * 100.0;
            if (g_TotalRounds > 1) {
                // 显示多轮进度 / Display progress for multiple rounds
                StringCchPrintf(text, 256, _T("Executing... Round %d / %d, Current Round Progress: %.2f%%"), g_CurrentRound, g_TotalRounds, percent);
            }
            else {
                // 显示单轮进度 / Display progress for single round
                StringCchPrintf(text, 256, _T("Executing... Progress: %.2f%%"), percent);
            }
        }
        else {
            StringCchCopy(text, 256, _T("Executing..."));
        }
        SetWindowText(GetDlgItem(g_hProgressDlg, ID_PROGRESS_TEXT), text);

        if (g_BytesFilled >= g_TotalBytesToFill && g_CurrentRound >= g_TotalRounds && !g_CancelOperation) {
            // 显示完成状态 / Display completion status
            StringCchCopy(text, 256, _T("Completed!"));
            SetWindowText(GetDlgItem(g_hProgressDlg, ID_PROGRESS_TEXT), text);
            EnableWindow(GetDlgItem(g_hProgressDlg, ID_BUTTON_OK), TRUE);
        }
    }
}

unsigned __stdcall OperationThread(void* param) {
    OperationParams* params = (OperationParams*)param;
    g_CancelOperation = FALSE;
    g_BytesFilled = 0;
    g_CurrentRound = 0;
    g_TotalRounds = params->times;

    // Get available disk space, reserving 1MB
    ULARGE_INTEGER freeBytes;
    GetDiskFreeSpaceEx(params->drivePath, &freeBytes, NULL, NULL);
    long long availableBytes = freeBytes.QuadPart - 1024 * 1024; // 留1MB / Reserve 1MB
    g_TotalBytesToFill = availableBytes;

    if (params->isGenerate) {
        g_TotalRounds = 1;
        long long requestedBytes = params->sizeKB * 1024LL;
        if (requestedBytes > availableBytes) {
            requestedBytes = availableBytes;
        }
        g_TotalBytesToFill = requestedBytes;
        BOOL success = FillAndDeleteOneRound(params->drivePath, &g_BytesFilled, g_TotalBytesToFill);
        if (!success && !g_CancelOperation) {
            TCHAR filePath[MAX_PATH];
            // 清理失败文件 / Clean up failed file
            StringCchPrintf(filePath, MAX_PATH, _T("%sfillfile.tmp"), params->drivePath);
            DeleteFile(filePath);
        }
    }
    else {
        for (g_CurrentRound = 1; g_CurrentRound <= params->times; ++g_CurrentRound) {
            if (g_CancelOperation) break;
            g_BytesFilled = 0;
            g_TotalBytesToFill = availableBytes;
            BOOL success = FillAndDeleteOneRound(params->drivePath, &g_BytesFilled, g_TotalBytesToFill);
            if (!success && !g_CancelOperation) {
                TCHAR filePath[MAX_PATH];
                // 清理失败文件 / Clean up failed file
                StringCchPrintf(filePath, MAX_PATH, _T("%sfillfile.tmp"), params->drivePath);
                DeleteFile(filePath);
            }
            if (g_CancelOperation) {
                TCHAR filePath[MAX_PATH];
                // 终止时清理 / Clean up on cancellation
                StringCchPrintf(filePath, MAX_PATH, _T("%sfillfile.tmp"), params->drivePath);
                DeleteFile(filePath);
                break;
            }
        }
    }

    UpdateProgressText(); // 确保显示“完成”或保持当前状态 / Ensure "Completed" is displayed or maintain current state
    delete params;
    return 0;
}

BOOL FillAndDeleteOneRound(const TCHAR* drivePath, long long* bytesFilled, long long totalToFill) {
    TCHAR filePath[MAX_PATH];
    StringCchPrintf(filePath, MAX_PATH, _T("%sfillfile.tmp"), drivePath);

    // Create temporary file for writing random data
    HANDLE hFile = CreateFile(filePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    const int blockSize = 1024 * 1024;
    BYTE* buffer = (BYTE*)malloc(blockSize);
    if (!buffer) {
        CloseHandle(hFile);
        return FALSE;
    }

    // Initialize random seed for generating random data
    srand((unsigned)time(NULL));
    long long written = 0;
    BOOL success = TRUE;
    while (written < totalToFill && !g_CancelOperation) {
        int writeSize = (int)min(blockSize, totalToFill - written);
        // Fill buffer with random bytes
        for (int j = 0; j < writeSize; j++) {
            buffer[j] = (BYTE)rand();
        }
        DWORD bytesWritten;
        if (!WriteFile(hFile, buffer, writeSize, &bytesWritten, NULL) || bytesWritten != writeSize) {
            success = FALSE;
            break;
        }
        written += bytesWritten;
        *bytesFilled = written;
    }

    free(buffer);
    if (CloseHandle(hFile)) {
        if (!g_CancelOperation && success) {
            // 完成时删除 / Delete file upon completion
            DeleteFile(filePath);
        }
        else if (g_CancelOperation || !success) {
            // 终止或失败时删除 / Delete file on cancellation or failure
            DeleteFile(filePath);
        }
    }
    else {
        // 关闭失败也尝试删除 / Attempt to delete file if closing fails
        DeleteFile(filePath);
    }

    return success;
}