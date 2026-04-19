#include "Helpers.hpp"
#include <d3d11.h>
#include <cstdio>
#include <vector>
#include <string>
#include <fstream>

#define IMGUI_DEFINE_MATH_OPERATORS

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

#pragma comment( lib, "d3d11.lib" )

static ID3D11Device* GDevice = nullptr;
static ID3D11DeviceContext* GDeviceContext = nullptr;
static IDXGISwapChain* GSwapChain = nullptr;
static ID3D11RenderTargetView* GRenderTarget = nullptr;
static HWND GWindow = nullptr;
static bool GRunning = true;
static bool GAlwaysOnTop = false;
static bool GCaptureKeyMode = false;
static const char* GKeyCaptureStatus = "Click 'Press key now' to capture.";

struct DetectResultT {
    bool Hit = false;
    int X = 0;
    int Y = 0;
    float Score = 0.f;
};

struct YoloCtxT {
    cv::dnn::Net Net;
    std::vector< std::string > Classes;
    int PersonClassId = 0;
    int InputSize = 640;
    bool Ready = false;
};

struct DetectCacheT {
    DetectResultT Last;
    std::chrono::high_resolution_clock::time_point LastRun{ };
};

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

static void CreateRenderTarget( ) {
    ID3D11Texture2D* BackBuffer = nullptr;
    GSwapChain->GetBuffer( 0, IID_PPV_ARGS( &BackBuffer ) );
    GDevice->CreateRenderTargetView( BackBuffer, nullptr, &GRenderTarget );
    BackBuffer->Release( );
}

static void CleanupRenderTarget( ) {
    if ( GRenderTarget ) {
        GRenderTarget->Release( );
        GRenderTarget = nullptr;
    }
}

static bool CreateDeviceD3D( HWND Hwnd ) {
    DXGI_SWAP_CHAIN_DESC Sd{ };
    Sd.BufferCount = 2;
    Sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Sd.OutputWindow = Hwnd;
    Sd.SampleDesc.Count = 1;
    Sd.Windowed = TRUE;
    Sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL FeatureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL FeatureLevel;
    UINT Flags = 0;

    if ( D3D11CreateDeviceAndSwapChain( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, Flags,
             FeatureLevels, 2, D3D11_SDK_VERSION, &Sd, &GSwapChain, &GDevice, &FeatureLevel, &GDeviceContext ) != S_OK ) {
        return false;
    }

    CreateRenderTarget( );
    return true;
}

static void CleanupDeviceD3D( ) {
    CleanupRenderTarget( );
    if ( GSwapChain ) {
        GSwapChain->Release( );
        GSwapChain = nullptr;
    }
    if ( GDeviceContext ) {
        GDeviceContext->Release( );
        GDeviceContext = nullptr;
    }
    if ( GDevice ) {
        GDevice->Release( );
        GDevice = nullptr;
    }
}

static LRESULT CALLBACK WndProc( HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam ) {
    if ( ImGui_ImplWin32_WndProcHandler( Hwnd, Msg, WParam, LParam ) )
        return true;

    switch ( Msg ) {
    case WM_SIZE:
        if ( GDevice != nullptr && WParam != SIZE_MINIMIZED ) {
            CleanupRenderTarget( );
            GSwapChain->ResizeBuffers( 0, LOWORD( LParam ), HIWORD( LParam ), DXGI_FORMAT_UNKNOWN, 0 );
            CreateRenderTarget( );
        }
        return 0;
    case WM_SYSCOMMAND:
        if ( ( WParam & 0xfff0 ) == SC_KEYMENU )
            return 0;
        break;
    case WM_DESTROY:
        GRunning = false;
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcA( Hwnd, Msg, WParam, LParam );
}

static bool InitImGuiWindow( ) {
    WNDCLASSEXA Wc{ sizeof( WNDCLASSEXA ), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandleA( nullptr ), nullptr, nullptr, nullptr, nullptr, "ColorbotImGuiWindow", nullptr };
    RegisterClassExA( &Wc );

    GWindow = CreateWindowA( Wc.lpszClassName, "Colorbot - ImGui Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        80, 80, 560, 380,
        nullptr, nullptr, Wc.hInstance, nullptr );
    if ( GWindow == nullptr )
        return false;

    if ( !CreateDeviceD3D( GWindow ) ) {
        CleanupDeviceD3D( );
        DestroyWindow( GWindow );
        UnregisterClassA( Wc.lpszClassName, Wc.hInstance );
        return false;
    }

    ShowWindow( GWindow, SW_SHOWDEFAULT );
    UpdateWindow( GWindow );

    IMGUI_CHECKVERSION( );
    ImGui::CreateContext( );
    ImGuiIO& Io = ImGui::GetIO( );
    Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark( );

    ImGui_ImplWin32_Init( GWindow );
    ImGui_ImplDX11_Init( GDevice, GDeviceContext );
    return true;
}

static void ShutdownImGuiWindow( ) {
    ImGui_ImplDX11_Shutdown( );
    ImGui_ImplWin32_Shutdown( );
    ImGui::DestroyContext( );

    CleanupDeviceD3D( );
    if ( GWindow )
        DestroyWindow( GWindow );
    UnregisterClassA( "ColorbotImGuiWindow", GetModuleHandleA( nullptr ) );
}

static bool PumpWindowMessages( ) {
    MSG Msg;
    while ( PeekMessageA( &Msg, nullptr, 0U, 0U, PM_REMOVE ) ) {
        TranslateMessage( &Msg );
        DispatchMessageA( &Msg );
        if ( Msg.message == WM_QUIT )
            return false;
    }
    return GRunning;
}

static bool RenderSettingsUi( ConfigT* Cfg, bool* SaveClicked ) {
    *SaveClicked = false;

    ImGui_ImplDX11_NewFrame( );
    ImGui_ImplWin32_NewFrame( );
    ImGui::NewFrame( );

    ImGui::SetNextWindowSize( ImVec2( 520, 320 ), ImGuiCond_Once );
    ImGui::Begin( "Colorbot Settings", nullptr, ImGuiWindowFlags_NoCollapse );

    ImGui::Separator( );

    ImGui::SliderInt( "Detection Size", &Cfg->DetectionSize, 0, 300 );
    ImGui::SliderInt( "YOLO Confidence (%)", &Cfg->YoloConfidence, 1, 99 );
    ImGui::SliderInt( "Click Cooldown (ms)", &Cfg->ClickCooldownMs, 1, 500 );
    ImGui::SliderInt( "Scan Stride", &Cfg->ScanStride, 1, 10 );

    if ( ImGui::Button( "Reset to Defaults" ) ) {
        ConfigT Default{ };
        *Cfg = Default;
    }

    if ( ImGui::Button( GCaptureKeyMode ? "Press any key..." : "Press key now" ) ) {
        GCaptureKeyMode = true;
        GKeyCaptureStatus = "Waiting for key press...";
    }
    ImGui::SameLine( );
    ImGui::TextUnformatted( GKeyCaptureStatus );

    if ( GCaptureKeyMode ) {
        for ( int Vk = 1; Vk <= 255; ++Vk ) {
            if ( GetAsyncKeyState( Vk ) & 1 ) {
                Cfg->ActivationKey = Vk;
                GCaptureKeyMode = false;
                GKeyCaptureStatus = "Captured activation key.";
                break;
            }
        }
    }

    if ( ImGui::Checkbox( "Always on top", &GAlwaysOnTop ) ) {
        SetWindowPos( GWindow, GAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
            0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );
    }

    ImGui::Text( "Current key: %s", VkToString( Cfg->ActivationKey ) );
    ImGui::TextDisabled( "Hold activation key to run person detection." );

    if ( ImGui::Button( "Save Config" ) ) {
        if ( WriteDefaultConfig( "config.txt", Cfg ) )
            printf( "[CFG] saved config.txt\n" );
        else
            printf( "[ERROR] failed to save config.txt\n" );
        *SaveClicked = true;
    }
    ImGui::SameLine( );
    ImGui::TextUnformatted( "Changes are live even before save." );

    ImGui::End( );

    ImGui::Render( );
    const float ClearColor[ 4 ] = { 0.08f, 0.08f, 0.10f, 1.f };
    GDeviceContext->OMSetRenderTargets( 1, &GRenderTarget, nullptr );
    GDeviceContext->ClearRenderTargetView( GRenderTarget, ClearColor );
    ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData( ) );
    GSwapChain->Present( 1, 0 );
    return true;
}

inline bool LoadClasses( const char* Path, std::vector< std::string >* Out ) {
    std::ifstream In( Path );
    if ( !In.is_open( ) )
        return false;

    Out->clear( );
    std::string Line;
    while ( std::getline( In, Line ) ) {
        if ( !Line.empty( ) && Line.back( ) == '\r' )
            Line.pop_back( );
        if ( !Line.empty( ) )
            Out->push_back( Line );
    }
    return !Out->empty( );
}

inline bool InitYolo( YoloCtxT* Yolo ) {
    if ( !LoadClasses( "models/coco.names", &Yolo->Classes ) ) {
        printf( "[ERROR] failed to load classes\n" );
        return false;
    }

    Yolo->PersonClassId = -1;
    for ( size_t i = 0; i < Yolo->Classes.size( ); ++i ) {
        if ( Yolo->Classes[ i ] == "person" ) {
            Yolo->PersonClassId = ( int )i;
            break;
        }
    }

    if ( Yolo->PersonClassId == -1 ) {
        printf( "[ERROR] 'person' class not found\n" );
        return false;
    }

    printf( "[YOLO] person class id = %d\n", Yolo->PersonClassId );

    try {
        Yolo->Net = cv::dnn::readNetFromDarknet(
            "models/yolov4-tiny.cfg",
            "models/yolov4-tiny.weights" );
    } catch ( const cv::Exception& E ) {
        printf( "[ERROR] yolov4 load fail: %s\n", E.what( ) );
        return false;
    }

    if ( Yolo->Net.empty( ) )
        return false;

    Yolo->Net.setPreferableBackend( cv::dnn::DNN_BACKEND_OPENCV );
    Yolo->Net.setPreferableTarget( cv::dnn::DNN_TARGET_CPU );

    Yolo->Ready = true;
    return true;
}

inline DetectResultT DetectYolo( CaptureCtxT* Ctx, ConfigT* Cfg, YoloCtxT* Yolo ) {
    DetectResultT Result{ };
    if ( !Yolo->Ready )
        return Result;

    if ( Cfg->DetectionSize < 32 )
        Cfg->DetectionSize = 32;

    POINT Cursor;
    GetCursorPos( &Cursor );

    int Half = Ctx->Size >> 1;

    if ( !BitBlt( Ctx->MemDc, 0, 0,
             Ctx->Size, Ctx->Size,
             Ctx->ScreenDc,
             Cursor.x - Half, Cursor.y - Half,
             SRCCOPY ) )
        return Result;

    BITMAPINFO Bmi{ };
    Bmi.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
    Bmi.bmiHeader.biWidth = Ctx->Size;
    Bmi.bmiHeader.biHeight = -Ctx->Size;
    Bmi.bmiHeader.biPlanes = 1;
    Bmi.bmiHeader.biBitCount = 32;

    if ( !GetDIBits( Ctx->MemDc, Ctx->Bmp, 0,
             Ctx->Size,
             Ctx->Buffer,
             &Bmi, DIB_RGB_COLORS ) )
        return Result;

    cv::Mat Bgra( Ctx->Size, Ctx->Size, CV_8UC4, Ctx->Buffer );
    cv::Mat Frame;
    cv::cvtColor( Bgra, Frame, cv::COLOR_BGRA2BGR );

    if ( Frame.empty( ) ) {
        printf( "[ERROR] empty frame\n" );
        return Result;
    }

    cv::Mat InputBlob = cv::dnn::blobFromImage( Frame, 1.f / 255.f, cv::Size( Yolo->InputSize, Yolo->InputSize ), cv::Scalar( ), true, false );
    Yolo->Net.setInput( InputBlob );

    std::vector< cv::Mat > Outs;
    Yolo->Net.forward( Outs, Yolo->Net.getUnconnectedOutLayersNames( ) );
    if ( Outs.empty( ) )
        return Result;

    float BestConf = 0.f;
    int BestX = 0, BestY = 0;
    int CenterX = Ctx->Size >> 1;
    int CenterY = Ctx->Size >> 1;
    float Threshold = float( Cfg->YoloConfidence ) / 100.f;

    for ( const cv::Mat& Out : Outs ) {
        const float* Data = ( float* )Out.data;

        for ( int I = 0; I < Out.rows; ++I, Data += Out.cols ) {
            float ObjConf = Data[ 4 ];
            if ( ObjConf < 0.01f )
                continue;

            int ClassId = -1;
            float ClassScore = 0.f;

            for ( int C = 5; C < Out.cols; ++C ) {
                if ( Data[ C ] > ClassScore ) {
                    ClassScore = Data[ C ];
                    ClassId = C - 5;
                }
            }

            float Conf = ObjConf * ClassScore;

            if ( Conf > 0.15f ) {
                printf( "[DBG] class=%d conf=%.2f\n", ClassId, Conf );
            }

            if ( ClassId != Yolo->PersonClassId )
                continue;

            if ( Conf < Threshold )
                continue;

            float Cx = Data[ 0 ];
            float Cy = Data[ 1 ];

            int ScreenX = Cursor.x - Half + int( Cx * Ctx->Size / Yolo->InputSize );
            int ScreenY = Cursor.y - Half + int( Cy * Ctx->Size / Yolo->InputSize );

            if ( Conf > BestConf ) {
                BestConf = Conf;
                BestX = ScreenX;
                BestY = ScreenY;
            }
        }
    }

    if ( BestConf >= Threshold ) {
        Result.Hit = true;
        Result.Score = BestConf;
        Result.X = BestX;
        Result.Y = BestY;
    }
    return Result;
}

inline DetectResultT DetectYoloThrottled( CaptureCtxT* Ctx, ConfigT* Cfg, YoloCtxT* Yolo, DetectCacheT* Cache ) {
    return DetectYolo( Ctx, Cfg, Yolo );
}

int main( ) {
    ConfigT Cfg{ };
    LoadConfig( "config.txt", &Cfg );

    printf(
        "[CFG INIT] size=%d key=%d conf=%d cooldown=%d stride=%d yolo=%d\n",
        Cfg.DetectionSize,
        Cfg.ActivationKey,
        Cfg.ColorTolerance,
        Cfg.ClickCooldownMs,
        Cfg.ScanStride,
        Cfg.YoloConfidence );

    if ( !InitImGuiWindow( ) ) {
        printf( "[ERROR] failed to init imgui window\n" );
        return 1;
    }

    YoloCtxT Yolo{ };
    if ( !InitYolo( &Yolo ) )
        return 1;

    printf( "hold %s to enable wallahi config\n", VkToString( Cfg.ActivationKey ) );

    CaptureCtxT Ctx{ };
    if ( !Ctx.Init( Cfg.DetectionSize ) )
        return 1;

    int LastDetectionSize = Cfg.DetectionSize;
    auto LastPrint = std::chrono::high_resolution_clock::now( );
    DetectCacheT DetectCache{ };

    while ( PumpWindowMessages( ) ) {
        bool SaveClicked = false;
        RenderSettingsUi( &Cfg, &SaveClicked );

        if ( Cfg.DetectionSize != LastDetectionSize && Cfg.DetectionSize > 2 ) {
            Ctx.Destroy( );
            if ( !Ctx.Init( Cfg.DetectionSize ) ) {
                printf( "[ERROR] failed to resize capture to %d\n", Cfg.DetectionSize );
                return 1;
            }

            LastDetectionSize = Cfg.DetectionSize;
            printf( "[CFG] detection_size=%d\n", LastDetectionSize );
        }

        if ( IsKeyPressed( Cfg.ActivationKey ) ) {
            DetectResultT Hit = DetectYoloThrottled( &Ctx, &Cfg, &Yolo, &DetectCache );
            if ( Hit.Hit ) {
                auto Now = std::chrono::high_resolution_clock::now( );
                auto Ms = std::chrono::duration_cast< std::chrono::milliseconds >( Now - LastPrint ).count( );
                printf( "[HIT] confidence=%.2f x=%d y=%d\n", Hit.Score, Hit.X, Hit.Y );
                DrawDetectBox( Hit.X, Hit.Y );
                if ( Ms >= Cfg.ClickCooldownMs ) {
                    printf( "[PERSON] confidence=%.1f%% center_inside=true\n", Hit.Score * 100.f );
                    LastPrint = Now;
                }
            }
        }

        Sleep( 8 );
    }

    Ctx.Destroy( );
    ShutdownImGuiWindow( );
    return 0;
}