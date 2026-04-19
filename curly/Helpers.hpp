#define NOMINMAX
#include <Windows.h>
#include <cstdint>
#include <chrono>
#include <cmath>

struct ConfigT {
    int DetectionSize = 640;
    int ActivationKey = VK_CONTROL;
    int ColorTolerance = 40;
    int ClickCooldownMs = 20;
    int ScanStride = 2;
    int YoloConfidence = 55;
};

// The code below is for a colorbot. Uncomment if you would like to replace the current dtcs with this
//
//struct TargetT {
//    uint8_t R, G, B;
//    float HueCenter;
//    float HueTolerance;
//};
//
//static TargetT Targets[] = {
//    { 128, 0, 128, 300.f, 40.f },
//    { 255, 0, 0, 0.f, 25.f },
//    { 255, 50, 50, 350.f, 25.f },
//    { 255, 80, 0, 20.f, 25.f },
//    { 255, 0, 120, 330.f, 30.f },
//    { 0, 255, 0, 120.f, 25.f },
//    { 0, 0, 255, 240.f, 60.f },
//    { 0, 120, 255, 210.f, 50.f } };
//
inline bool IsKeyPressed( int Vk ) {
   /* we definitely need an entire function for this brother muhammed */
    return ( GetAsyncKeyState( Vk ) & 0x8000 ) != 0;
}
//
//inline int ColorDistSq( uint8_t R1, uint8_t G1, uint8_t B1,
//    uint8_t R2, uint8_t G2, uint8_t B2 ) {
//    int Dr = int( R1 ) - int( R2 );
//    int Dg = int( G1 ) - int( G2 );
//    int Db = int( B1 ) - int( B2 );
//    return Dr * Dr + Dg * Dg + Db * Db;
//}
//
//inline float RgbToHue( uint8_t R, uint8_t G, uint8_t B ) {
//    float Rf = R / 255.f;
//    float Gf = G / 255.f;
//    float Bf = B / 255.f;
//
//    float Maxv = ( Rf > Gf ? ( Rf > Bf ? Rf : Bf ) : ( Gf > Bf ? Gf : Bf ) );
//    float Minv = ( Rf < Gf ? ( Rf < Bf ? Rf : Bf ) : ( Gf < Bf ? Gf : Bf ) );
//    float Delta = Maxv - Minv;
//
//    if ( Delta < 0.0001f )
//        return 0.f;
//
//    float Hue;
//
//    if ( Maxv == Rf )
//        Hue = 60.f * fmodf( ( ( Gf - Bf ) / Delta ), 6.f );
//    else if ( Maxv == Gf )
//        Hue = 60.f * ( ( ( Bf - Rf ) / Delta ) + 2.f );
//    else
//        Hue = 60.f * ( ( ( Rf - Gf ) / Delta ) + 4.f );
//
//    return ( Hue < 0.f ) ? Hue + 360.f : Hue;
//}

inline int ParseInt( const char* S ) {
    int V = 0;
    while ( *S >= '0' && *S <= '9' ) {
        V = V * 10 + ( *S - '0' );
        ++S;
    }
    return V;
}

bool WriteDefaultConfig( const char* Path, const ConfigT* Cfg ) {
    HANDLE H = CreateFileA( Path, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

    if ( H == INVALID_HANDLE_VALUE )
        return false;

    char Buffer[ 256 ];

    int Len = wsprintfA(
        Buffer,
        "detection_size=%d\n"
        "activation_key=%d\n"
        "color_tolerance=%d\n"
        "click_cooldown_ms=%d\n"
        "scan_stride=%d\n"
        "yolo_confidence=%d\n",
        Cfg->DetectionSize,
        Cfg->ActivationKey,
        Cfg->ColorTolerance,
        Cfg->ClickCooldownMs,
        Cfg->ScanStride,
        Cfg->YoloConfidence );

    DWORD Written;
    WriteFile( H, Buffer, Len, &Written, nullptr );
    CloseHandle( H );

    return true;
}

inline const char* VkToString( int vk ) {
    static char name[ 64 ];

    UINT scan = MapVirtualKeyA( vk, MAPVK_VK_TO_VSC );
    LONG lParam = ( scan << 16 );

    if ( GetKeyNameTextA( lParam, name, sizeof( name ) ) > 0 )
        return name;

    wsprintfA( name, "VK_%d", vk );
    return name;
}

inline void DrawDetectBox( int x, int y ) { /* u can remove this if you like. */
    HDC dc = GetDC( nullptr );

    RECT r;
    r.left = x - 3;
    r.top = y - 3;
    r.right = x + 3;
    r.bottom = y + 3;

    HBRUSH brush = CreateSolidBrush( RGB( 255, 0, 0 ) );
    FrameRect( dc, &r, brush );

    DeleteObject( brush );
    ReleaseDC( nullptr, dc );
}

void LoadConfig( const char* Path, ConfigT* Cfg ) {
    HANDLE H = CreateFileA( Path, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );

    if ( H == INVALID_HANDLE_VALUE ) {
        WriteDefaultConfig( Path, Cfg );
        printf( "[CFG] created default config\n" );
        return;
    }

    DWORD Size = GetFileSize( H, nullptr );
    if ( Size == INVALID_FILE_SIZE || Size == 0 ) {
        CloseHandle( H );
        WriteDefaultConfig( Path, Cfg );
        printf( "[CFG] empty config, reset to defaults\n" );
        return;
    }

    char* Buffer = ( char* )VirtualAlloc( nullptr, Size + 1,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE );

    DWORD Read;
    ReadFile( H, Buffer, Size, &Read, nullptr );
    Buffer[ Size ] = 0;

    CloseHandle( H );

    char* Ptr = Buffer;

    while ( *Ptr ) {
        char* Key = Ptr;

        while ( *Ptr && *Ptr != '=' )
            ++Ptr;
        if ( !*Ptr )
            break;

        *Ptr++ = 0;
        char* Value = Ptr;

        while ( *Ptr && *Ptr != '\n' && *Ptr != '\r' )
            ++Ptr;

        if ( *Ptr )
            *Ptr++ = 0;

        int V = ParseInt( Value );

        if ( lstrcmpA( Key, "detection_size" ) == 0 )
            Cfg->DetectionSize = V;
        else if ( lstrcmpA( Key, "activation_key" ) == 0 )
            Cfg->ActivationKey = V;
        else if ( lstrcmpA( Key, "color_tolerance" ) == 0 )
            Cfg->ColorTolerance = V;
        else if ( lstrcmpA( Key, "click_cooldown_ms" ) == 0 )
            Cfg->ClickCooldownMs = V;
        else if ( lstrcmpA( Key, "scan_stride" ) == 0 )
            Cfg->ScanStride = V;
        else if ( lstrcmpA( Key, "yolo_confidence" ) == 0 )
            Cfg->YoloConfidence = V;
    }

    VirtualFree( Buffer, 0, MEM_RELEASE );
}

struct CaptureCtxT {
    HDC ScreenDc = nullptr;
    HDC MemDc = nullptr;
    HBITMAP Bmp = nullptr;
    uint8_t* Buffer = nullptr;
    int Size = 0;

    bool Init( int S ) {
        Size = S;

        ScreenDc = GetDC( nullptr );
        MemDc = CreateCompatibleDC( ScreenDc );
        Bmp = CreateCompatibleBitmap( ScreenDc, Size, Size );

        if ( !ScreenDc || !MemDc || !Bmp )
            return false;

        SelectObject( MemDc, Bmp );

        Buffer = ( uint8_t* )VirtualAlloc( nullptr,
            Size * Size * 4,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE );

        return Buffer != nullptr;
    }

    void Destroy( ) {
        if ( Buffer )
            VirtualFree( Buffer, 0, MEM_RELEASE );
        if ( Bmp )
            DeleteObject( Bmp );
        if ( MemDc )
            DeleteDC( MemDc );
        if ( ScreenDc )
            ReleaseDC( nullptr, ScreenDc );
    }
};