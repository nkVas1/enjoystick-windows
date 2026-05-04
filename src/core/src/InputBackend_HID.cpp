// WIN32_LEAN_AND_MEAN / NOMINMAX injected by CMake.
#include "InputBackend_HID.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace enjoystick::core {

// ---------------------------------------------------------------------------
// Constants — DualSense USB Input Report 0x01 byte offsets
// ---------------------------------------------------------------------------
// All offsets are relative to the 64-byte payload that follows the 1-byte
// report-ID prefix returned by ReadFile (total buffer = 65 bytes).
// Ref: https://controllers.fandom.com/wiki/Sony_DualSense

static constexpr size_t kRptSize        = 65;   // 1 report-id + 64 payload
static constexpr size_t kOff_LX         = 1;    // left stick X  (uint8)
static constexpr size_t kOff_LY         = 2;    // left stick Y  (uint8)
static constexpr size_t kOff_RX         = 3;    // right stick X (uint8)
static constexpr size_t kOff_RY         = 4;    // right stick Y (uint8)
static constexpr size_t kOff_LT         = 5;    // left trigger  (uint8)
static constexpr size_t kOff_RT         = 6;    // right trigger (uint8)
static constexpr size_t kOff_Btn0       = 8;    // buttons byte 0  (DPAD + square/x/circle/tri)
static constexpr size_t kOff_Btn1       = 9;    // buttons byte 1  (L1/R1/L2/R2/share/options/L3/R3)
static constexpr size_t kOff_Btn2       = 10;   // buttons byte 2  (PS/touchpad/mute)
static constexpr size_t kOff_Touch0     = 33;   // touchpad finger0 packet (4 bytes)

// BT report is 78 bytes (report ID 0x31), data shifted by +2
static constexpr size_t kRptSizeBt      = 78;

// DualSense Output Report 0x02 (USB) layout (minimal, motors only)
// Byte 0: report ID = 0x02
// Byte 1: flags — 0xFF (enable all)
// Byte 2: flags2 — 0x00
// Byte 3: right rumble (high-freq)
// Byte 4: left  rumble (low-freq)
static constexpr size_t kOutRptSize     = 64;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static float NormaliseAxis(uint8_t raw) noexcept {
    return (static_cast<float>(raw) - 128.0f) / 128.0f;
}
static float NormaliseTrigger(uint8_t raw) noexcept {
    return static_cast<float>(raw) / 255.0f;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

HidBackend::HidBackend(Config cfg, DeadzoneFilter filter)
    : m_config(std::move(cfg))
    , m_deadzone(std::move(filter))
{}

HidBackend::~HidBackend() {
    Stop();
    if (m_device != INVALID_HANDLE_VALUE) {
        CloseHandle(m_device);
        m_device = INVALID_HANDLE_VALUE;
    }
}

// ---------------------------------------------------------------------------
// TryOpen — enumerate HID devices, find first DualSense, open it
// ---------------------------------------------------------------------------

bool HidBackend::TryOpen() {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevsW(
        &hidGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return false;

    SP_DEVICE_INTERFACE_DATA ifaceData{};
    ifaceData.cbSize = sizeof(ifaceData);

    bool found = false;
    for (DWORD idx = 0;
         SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, idx, &ifaceData);
         ++idx)
    {
        // Get required buffer size
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData, nullptr, 0, &needed, nullptr);

        std::vector<BYTE> buf(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData,
                detail, needed, nullptr, nullptr))
            continue;

        // Convert path to narrow for storage; keep wide for CreateFile
        const wchar_t* pathW = detail->DevicePath;

        // Quick VID/PID check via string scan before opening
        std::wstring pathStr(pathW);
        // Paths contain "vid_054c" and "pid_0ce6" or "pid_0df2"
        auto toLower = [](std::wstring s) {
            for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
            return s;
        };
        const std::wstring lower = toLower(pathStr);
        if (lower.find(L"vid_054c") == std::wstring::npos) continue;
        if (lower.find(L"pid_0ce6") == std::wstring::npos &&
            lower.find(L"pid_0df2") == std::wstring::npos) continue;

        HANDLE h = CreateFileW(
            pathW,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            nullptr);

        if (h == INVALID_HANDLE_VALUE) {
            // Try read-only (some security policies block write)
            h = CreateFileW(pathW, GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        }
        if (h == INVALID_HANDLE_VALUE) continue;

        // Check capabilities — report size distinguishes USB vs BT
        PHIDP_PREPARSED_DATA ppd = nullptr;
        if (!HidD_GetPreparsedData(h, &ppd)) { CloseHandle(h); continue; }

        HIDP_CAPS caps{};
        HidP_GetCaps(ppd, &caps);
        HidD_FreePreparsedData(ppd);

        const bool isBt = (caps.InputReportByteLength >= kRptSizeBt);

        // Store result
        m_device     = h;
        m_isBt       = isBt;
        // Store narrow path
        const int sz = WideCharToMultiByte(CP_UTF8, 0, pathW, -1, nullptr, 0, nullptr, nullptr);
        m_devicePath.resize(static_cast<size_t>(sz));
        WideCharToMultiByte(CP_UTF8, 0, pathW, -1, m_devicePath.data(), sz, nullptr, nullptr);
        found = true;
        break;
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return found;
}

// ---------------------------------------------------------------------------
// Start / Stop
// ---------------------------------------------------------------------------

void HidBackend::Start() {
    if (m_running.exchange(true)) return;
    m_thread = std::thread([this] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        if (m_device != INVALID_HANDLE_VALUE) {
            {
                std::unique_lock lk(m_stateMutex);
                m_connected = true;
            }
            FireConnection(ControllerId{0}, ConnectionEvent::Connected);
        }
        PollLoop();
    });
}

void HidBackend::Stop() {
    if (!m_running.exchange(false)) return;
    // CancelIo unblocks pending ReadFile on m_device
    if (m_device != INVALID_HANDLE_VALUE) CancelIoEx(m_device, nullptr);
    if (m_thread.joinable()) m_thread.join();
    if (m_connected) {
        m_connected = false;
        FireConnection(ControllerId{0}, ConnectionEvent::Disconnected);
    }
}

// ---------------------------------------------------------------------------
// PollLoop
// ---------------------------------------------------------------------------

void HidBackend::PollLoop() {
    if (m_device == INVALID_HANDLE_VALUE) return;

    std::vector<uint8_t> buf(kRptSize + 16, 0);
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return;

    while (m_running.load(std::memory_order_relaxed)) {
        ResetEvent(ov.hEvent);
        DWORD bytesRead = 0;
        const BOOL ok = ReadFile(
            m_device, buf.data(),
            static_cast<DWORD>(m_isBt ? kRptSizeBt : kRptSize),
            &bytesRead, &ov);

        if (!ok) {
            const DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                // Wait up to 100 ms so Stop() can signal us via CancelIoEx
                const DWORD w = WaitForSingleObject(ov.hEvent, 100);
                if (w == WAIT_OBJECT_0)
                    GetOverlappedResult(m_device, &ov, &bytesRead, FALSE);
                else
                    continue;
            } else {
                // Device disconnected
                break;
            }
        }

        if (!m_running.load(std::memory_order_relaxed)) break;

        ControllerState newState;
        if (!ParseReport(buf.data(), static_cast<size_t>(bytesRead), newState))
            continue;

        {
            std::unique_lock lk(m_stateMutex);
            const uint32_t prev = static_cast<uint32_t>(m_state.buttons);
            const uint32_t curr = static_cast<uint32_t>(newState.buttons);
            newState.buttonsDown = static_cast<Button>(curr & ~prev);
            newState.buttonsUp   = static_cast<Button>(prev & ~curr);
            m_state = newState;
        }
        FireInput(newState);
    }

    CloseHandle(ov.hEvent);

    // Signal disconnect if still marked connected
    {
        std::unique_lock lk(m_stateMutex);
        if (m_connected) {
            m_connected = false;
            lk.unlock();
            FireConnection(ControllerId{0}, ConnectionEvent::Disconnected);
        }
    }
}

// ---------------------------------------------------------------------------
// ParseReport
// ---------------------------------------------------------------------------

bool HidBackend::ParseReport(
    const uint8_t* buf, size_t len,
    ControllerState& out) const noexcept
{
    // Minimum size check
    const size_t minLen = m_isBt ? kRptSizeBt : kRptSize;
    if (len < minLen) return false;

    // For BT, data is shifted by 2 bytes (report 0x31 + seq byte)
    const size_t shift = m_isBt ? 2u : 0u;

    const uint8_t* d = buf + shift;

    // Report ID must be 0x01 (USB) or 0x31 (BT, but we ignore BT for now)
    if (!m_isBt && d[0] != 0x01) return false;

    out.id = ControllerId{0};

    // Sticks
    out.leftStick.x  = NormaliseAxis(d[kOff_LX]);
    out.leftStick.y  = -NormaliseAxis(d[kOff_LY]);   // Y is inverted on DS
    out.rightStick.x = NormaliseAxis(d[kOff_RX]);
    out.rightStick.y = -NormaliseAxis(d[kOff_RY]);

    // Triggers
    out.leftTrigger  = NormaliseTrigger(d[kOff_LT]);
    out.rightTrigger = NormaliseTrigger(d[kOff_RT]);

    // ---- Buttons --------------------------------------------------------
    const uint8_t b0 = d[kOff_Btn0];
    const uint8_t b1 = d[kOff_Btn1];
    const uint8_t b2 = d[kOff_Btn2];

    // D-pad is low nibble of b0 (0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW, 8=neutral)
    const uint8_t dpad = b0 & 0x0F;
    if (dpad == 0 || dpad == 1 || dpad == 7) out.buttons = out.buttons | Button::DPadUp;
    if (dpad == 2 || dpad == 1 || dpad == 3) out.buttons = out.buttons | Button::DPadRight;
    if (dpad == 4 || dpad == 3 || dpad == 5) out.buttons = out.buttons | Button::DPadDown;
    if (dpad == 6 || dpad == 5 || dpad == 7) out.buttons = out.buttons | Button::DPadLeft;

    // Face buttons (high nibble of b0)
    if (b0 & 0x10) out.buttons = out.buttons | Button::West;   // Square
    if (b0 & 0x20) out.buttons = out.buttons | Button::South;  // Cross
    if (b0 & 0x40) out.buttons = out.buttons | Button::East;   // Circle
    if (b0 & 0x80) out.buttons = out.buttons | Button::North;  // Triangle

    // Shoulders / triggers as digital
    if (b1 & 0x01) out.buttons = out.buttons | Button::LB;       // L1
    if (b1 & 0x02) out.buttons = out.buttons | Button::RB;       // R1
    if (b1 & 0x04) out.buttons = out.buttons | Button::LT_Click; // L2
    if (b1 & 0x08) out.buttons = out.buttons | Button::RT_Click; // R2
    if (b1 & 0x10) out.buttons = out.buttons | Button::Select;   // Create/Share
    if (b1 & 0x20) out.buttons = out.buttons | Button::Start;    // Options
    if (b1 & 0x40) out.buttons = out.buttons | Button::LS;       // L3
    if (b1 & 0x80) out.buttons = out.buttons | Button::RS;       // R3

    // System
    if (b2 & 0x01) out.buttons = out.buttons | Button::Guide;    // PS
    // Touchpad button (b2 & 0x02) — not mapped to a named button yet
    // Mute button    (b2 & 0x04) — reserved

    // Trigger thresholds -> digital clicks (if analog not set above)
    if (out.leftTrigger  > 0.5f) out.buttons = out.buttons | Button::LT_Click;
    if (out.rightTrigger > 0.5f) out.buttons = out.buttons | Button::RT_Click;

    // Apply deadzone
    out.leftStick  = m_deadzone.Apply(out.leftStick);
    out.rightStick = m_deadzone.Apply(out.rightStick);

    // Timestamp
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    out.timestamp = static_cast<uint64_t>(qpc.QuadPart);

    return true;
}

// ---------------------------------------------------------------------------
// Rumble — Output Report 0x02
// ---------------------------------------------------------------------------

void HidBackend::SendRumbleReport(float low, float high) noexcept {
    if (m_device == INVALID_HANDLE_VALUE) return;
    if (!m_config.hapticsEnabled) return;

    // 64-byte output report (USB)
    uint8_t rpt[kOutRptSize] = {};
    rpt[0]  = 0x02;  // report ID
    rpt[1]  = 0xFF;  // valid flag: enable both motors
    rpt[2]  = 0x00;
    rpt[3]  = static_cast<uint8_t>(std::clamp(high, 0.0f, 1.0f) * 255.0f);  // right/high
    rpt[4]  = static_cast<uint8_t>(std::clamp(low,  0.0f, 1.0f) * 255.0f);  // left/low

    DWORD written = 0;
    WriteFile(m_device, rpt, static_cast<DWORD>(kOutRptSize), &written, nullptr);
}

void HidBackend::Rumble(ControllerId id, RumbleParams params) {
    if (id != ControllerId{0}) return;
    SendRumbleReport(params.lowFreqMotor, params.highFreqMotor);
}

// ---------------------------------------------------------------------------
// Callback registration
// ---------------------------------------------------------------------------

CallbackHandle HidBackend::OnInput(InputEventCallback cb) {
    std::unique_lock lk(m_callbackMutex);
    const uint64_t id = ++m_nextId;
    m_inputCbs.push_back({id, std::move(cb)});
    return CallbackHandle([this, id] {
        std::unique_lock l(m_callbackMutex);
        for (auto& e : m_inputCbs) if (e.id == id) { e.fn = nullptr; return; }
    });
}

CallbackHandle HidBackend::OnConnection(ConnectionCallback cb) {
    std::unique_lock lk(m_callbackMutex);
    const uint64_t id = ++m_nextId;
    m_connCbs.push_back({id, std::move(cb)});
    return CallbackHandle([this, id] {
        std::unique_lock l(m_callbackMutex);
        for (auto& e : m_connCbs) if (e.id == id) { e.fn = nullptr; return; }
    });
}

void HidBackend::FireInput(const ControllerState& s) {
    std::shared_lock lk(m_callbackMutex);
    for (const auto& e : m_inputCbs) if (e.fn) e.fn(s);
}

void HidBackend::FireConnection(ControllerId id, ConnectionEvent ev) {
    std::shared_lock lk(m_callbackMutex);
    for (const auto& e : m_connCbs) if (e.fn) e.fn(id, ev);
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

std::vector<ControllerInfo> HidBackend::GetConnectedControllers() const {
    std::shared_lock lk(m_stateMutex);
    if (!m_connected) return {};
    return {{ ControllerId{0}, ControllerType::DualSense, m_devicePath }};
}

ControllerState HidBackend::GetState(ControllerId id) const {
    if (id != ControllerId{0}) return {};
    std::shared_lock lk(m_stateMutex);
    return m_state;
}

} // namespace enjoystick::core
