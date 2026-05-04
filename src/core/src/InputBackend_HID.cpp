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
// Offsets are into the buffer returned by ReadFile (byte 0 = report ID).
// Ref: https://controllers.fandom.com/wiki/Sony_DualSense

static constexpr size_t kRptSize    = 65;   // 1 report-id + 64 payload
static constexpr size_t kOff_LX     = 1;
static constexpr size_t kOff_LY     = 2;
static constexpr size_t kOff_RX     = 3;
static constexpr size_t kOff_RY     = 4;
static constexpr size_t kOff_LT     = 5;
static constexpr size_t kOff_RT     = 6;
static constexpr size_t kOff_Btn0   = 8;
static constexpr size_t kOff_Btn1   = 9;
static constexpr size_t kOff_Btn2   = 10;
static constexpr size_t kRptSizeBt  = 78;   // Bluetooth report 0x31
static constexpr size_t kOutRptSize = 64;   // Output report size

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
// TryOpen
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
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData, nullptr, 0, &needed, nullptr);
        std::vector<BYTE> buf(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData, detail, needed, nullptr, nullptr))
            continue;

        const wchar_t* pathW = detail->DevicePath;
        std::wstring lower(pathW);
        for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));

        if (lower.find(L"vid_054c") == std::wstring::npos) continue;
        if (lower.find(L"pid_0ce6") == std::wstring::npos &&
            lower.find(L"pid_0df2") == std::wstring::npos) continue;

        HANDLE h = CreateFileW(pathW,
            GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            h = CreateFileW(pathW, GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        PHIDP_PREPARSED_DATA ppd = nullptr;
        if (!HidD_GetPreparsedData(h, &ppd)) { CloseHandle(h); continue; }
        HIDP_CAPS caps{};
        HidP_GetCaps(ppd, &caps);
        HidD_FreePreparsedData(ppd);

        m_device = h;
        m_isBt   = (caps.InputReportByteLength >= static_cast<USHORT>(kRptSizeBt));

        const int sz = WideCharToMultiByte(CP_UTF8, 0, pathW, -1, nullptr, 0, nullptr, nullptr);
        m_devicePath.assign(static_cast<size_t>(sz - 1), '\0');
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
            { std::unique_lock lk(m_stateMutex); m_connected = true; }
            FireConnection(ControllerId{0}, ConnectionEvent::Connected);
        }
        PollLoop();
    });
}

void HidBackend::Stop() {
    if (!m_running.exchange(false)) return;
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
        const BOOL ok = ReadFile(m_device, buf.data(),
            static_cast<DWORD>(m_isBt ? kRptSizeBt : kRptSize), &bytesRead, &ov);

        if (!ok) {
            const DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                const DWORD w = WaitForSingleObject(ov.hEvent, 100);
                if (w == WAIT_OBJECT_0)
                    GetOverlappedResult(m_device, &ov, &bytesRead, FALSE);
                else
                    continue;
            } else {
                break;  // device disconnected
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
    const uint8_t* buf, size_t len, ControllerState& out) const noexcept
{
    const size_t minLen = m_isBt ? kRptSizeBt : kRptSize;
    if (len < minLen) return false;

    const size_t shift = m_isBt ? 2u : 0u;
    const uint8_t* d = buf + shift;

    if (!m_isBt && d[0] != 0x01) return false;

    out.id = ControllerId{0};

    out.leftStick.x  =  NormaliseAxis(d[kOff_LX]);
    out.leftStick.y  = -NormaliseAxis(d[kOff_LY]);
    out.rightStick.x =  NormaliseAxis(d[kOff_RX]);
    out.rightStick.y = -NormaliseAxis(d[kOff_RY]);
    out.leftTrigger  =  NormaliseTrigger(d[kOff_LT]);
    out.rightTrigger =  NormaliseTrigger(d[kOff_RT]);

    const uint8_t b0 = d[kOff_Btn0];
    const uint8_t b1 = d[kOff_Btn1];
    const uint8_t b2 = d[kOff_Btn2];

    // D-pad
    const uint8_t dpad = b0 & 0x0F;
    if (dpad == 0 || dpad == 1 || dpad == 7) out.buttons = out.buttons | Button::DPadUp;
    if (dpad == 2 || dpad == 1 || dpad == 3) out.buttons = out.buttons | Button::DPadRight;
    if (dpad == 4 || dpad == 3 || dpad == 5) out.buttons = out.buttons | Button::DPadDown;
    if (dpad == 6 || dpad == 5 || dpad == 7) out.buttons = out.buttons | Button::DPadLeft;

    // Face
    if (b0 & 0x10) out.buttons = out.buttons | Button::West;   // Square
    if (b0 & 0x20) out.buttons = out.buttons | Button::South;  // Cross
    if (b0 & 0x40) out.buttons = out.buttons | Button::East;   // Circle
    if (b0 & 0x80) out.buttons = out.buttons | Button::North;  // Triangle

    // Shoulders
    if (b1 & 0x01) out.buttons = out.buttons | Button::LB;
    if (b1 & 0x02) out.buttons = out.buttons | Button::RB;
    if (b1 & 0x04) out.buttons = out.buttons | Button::LT_Click;
    if (b1 & 0x08) out.buttons = out.buttons | Button::RT_Click;
    if (b1 & 0x10) out.buttons = out.buttons | Button::Select;
    if (b1 & 0x20) out.buttons = out.buttons | Button::Start;
    if (b1 & 0x40) out.buttons = out.buttons | Button::LS;
    if (b1 & 0x80) out.buttons = out.buttons | Button::RS;

    // System / touchpad
    if (b2 & 0x01) out.buttons = out.buttons | Button::Guide;
    if (b2 & 0x02) out.buttons = out.buttons | Button::Touchpad;

    // Analog trigger thresholds -> digital click
    if (out.leftTrigger  > 0.5f) out.buttons = out.buttons | Button::LT_Click;
    if (out.rightTrigger > 0.5f) out.buttons = out.buttons | Button::RT_Click;

    out.leftStick  = m_deadzone.Apply(out.leftStick);
    out.rightStick = m_deadzone.Apply(out.rightStick);

    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    out.timestamp = static_cast<uint64_t>(qpc.QuadPart);
    return true;
}

// ---------------------------------------------------------------------------
// Rumble
// ---------------------------------------------------------------------------

void HidBackend::SendRumbleReport(float low, float high) noexcept {
    if (m_device == INVALID_HANDLE_VALUE || !m_config.hapticsEnabled) return;
    uint8_t rpt[kOutRptSize] = {};
    rpt[0] = 0x02;
    rpt[1] = 0xFF;
    rpt[2] = 0x00;
    rpt[3] = static_cast<uint8_t>(std::clamp(high, 0.0f, 1.0f) * 255.0f);
    rpt[4] = static_cast<uint8_t>(std::clamp(low,  0.0f, 1.0f) * 255.0f);
    DWORD written = 0;
    WriteFile(m_device, rpt, static_cast<DWORD>(kOutRptSize), &written, nullptr);
}

void HidBackend::Rumble(ControllerId id, RumbleParams params) {
    if (id != ControllerId{0}) return;
    SendRumbleReport(params.lowFreqMotor, params.highFreqMotor);
}

// ---------------------------------------------------------------------------
// Callbacks
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
    return {{ ControllerId{0}, ControllerType::PlayStation, m_devicePath }};
}

ControllerState HidBackend::GetState(ControllerId id) const {
    if (id != ControllerId{0}) return {};
    std::shared_lock lk(m_stateMutex);
    return m_state;
}

} // namespace enjoystick::core
