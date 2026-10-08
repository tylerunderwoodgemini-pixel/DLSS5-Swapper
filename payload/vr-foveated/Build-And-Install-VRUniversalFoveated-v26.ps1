param(
    [string]$GameDir = '',
    [string]$LocalNgxZip = '',
    [string]$OutputDir = '',
    [string]$LocalFeederZip = '',
    [string]$LocalVulkanZip = '',
    [switch]$BuildOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$Tag = 'v1.16.0-beta.4'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Work = Join-Path $env:TEMP ('DLSS5-VRFoveated-v26-' + $PID)
$Log = Join-Path $Root 'VR_FOVEATED_BUILD_LOG.txt'
$Out = if ($OutputDir) { [IO.Path]::GetFullPath($OutputDir) } else { Join-Path $Root 'READY_TO_INSTALL' }

if (-not $LocalNgxZip) {
    $candidates = @(
        (Join-Path $Root 'DLSS-main.zip'),
        (Join-Path (Split-Path $Root -Parent) 'DLSS-main.zip'),
        (Join-Path (Split-Path (Split-Path $Root -Parent) -Parent) 'DLSS-main.zip')
    )
    $LocalNgxZip = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}

# Unique work directory: never recursively remove a caller-supplied directory.
if (Test-Path -LiteralPath $Work) { throw "Build work directory already exists: $Work" }
New-Item -ItemType Directory -Path $Work -Force | Out-Null
New-Item -ItemType Directory -Path $Out -Force | Out-Null
Set-Content -LiteralPath $Log -Value '' -Encoding UTF8

function Say([string]$s) {
    Write-Host $s
    Add-Content -LiteralPath $Log -Value $s -Encoding UTF8
}
function Fail([string]$s) {
    Say ''
    Say ('ERROR: ' + $s)
    Say ('Log: ' + $Log)
    exit 1
}
function Replace-RegexOnce([string]$Text, [string]$Pattern, [string]$Replacement, [string]$Label) {
    $m = [regex]::Matches($Text, $Pattern, [Text.RegularExpressions.RegexOptions]::Multiline)
    if ($m.Count -ne 1) { Fail ($Label + ': expected exactly 1 match, found ' + $m.Count) }
    $x = $m[0]
    return $Text.Substring(0,$x.Index) + $Replacement + $Text.Substring($x.Index+$x.Length)
}
function Replace-LiteralOnce([string]$Text, [string]$Needle, [string]$Replacement, [string]$Label) {
    $i = $Text.IndexOf($Needle)
    if ($i -lt 0) { Fail ($Label + ': source anchor not found') }
    if ($Text.IndexOf($Needle, $i + $Needle.Length) -ge 0) { Fail ($Label + ': source anchor matched more than once') }
    return $Text.Substring(0,$i) + $Replacement + $Text.Substring($i+$Needle.Length)
}

try {
    Say 'VR UNIVERSAL CURRENT-FRAME FOVEATED BUILD v26'
    Say ('Source base: DLSS5-Feeder ' + $Tag)
    Say 'Default NR region: Balanced 60% width x 50% height per eye (30% pixels). Runtime presets/custom size are available in DLSS 5 Feed.'
    Say 'Outer left/right/top/bottom regions remain the current native frame.'
    Say 'DFC settings are NOT changed.'
    Say ''

    if (-not $BuildOnly) {
        if (-not $GameDir -or -not (Test-Path -LiteralPath $GameDir -PathType Container)) {
            Fail ('Game folder not found: ' + $GameDir)
        }
    }
    if (-not (Test-Path -LiteralPath $LocalNgxZip -PathType Leaf)) {
        Fail ('Local NVIDIA DLSS SDK ZIP not found: ' + $LocalNgxZip)
    }

    # ---- Official source
    $sourceZip = Join-Path $Work 'feeder.zip'
    $sourceExtract = Join-Path $Work 'feeder-src'
    Say 'Downloading official feeder source...'
    if ($LocalFeederZip) { Copy-Item -LiteralPath $LocalFeederZip -Destination $sourceZip }
    else { Invoke-WebRequest -UseBasicParsing -TimeoutSec 90 -Uri ('https://github.com/jlrouzies-fr/DLSS5-Feeder/archive/refs/tags/' + $Tag + '.zip') -OutFile $sourceZip }
    Expand-Archive -LiteralPath $sourceZip -DestinationPath $sourceExtract -Force
    $roots = @(Get-ChildItem -LiteralPath $sourceExtract -Directory | Where-Object { $_.Name -like 'DLSS5-Feeder-*' })
    if ($roots.Count -ne 1) { Fail 'Could not identify extracted feeder source.' }
    $srcRoot = $roots[0].FullName
    $cppPath = Join-Path $srcRoot 'src\dlss5-feed.cpp'
    if (-not (Test-Path -LiteralPath $cppPath)) { Fail 'src\dlss5-feed.cpp missing.' }

    # ---- NGX exactly like upstream build workflow
    Say 'Installing NGX SDK dependency from your local DLSS-main.zip...'
    $ngxExtract = Join-Path $Work 'ngx'
    Expand-Archive -LiteralPath $LocalNgxZip -DestinationPath $ngxExtract -Force
    $ngxRoots = @(Get-ChildItem -LiteralPath $ngxExtract -Directory | Where-Object { $_.Name -like 'DLSS-*' })
    if ($ngxRoots.Count -ne 1) { Fail 'Could not identify DLSS-main root inside local SDK ZIP.' }
    $nvidiaRoot = $ngxRoots[0].FullName
    $ngxDest = Join-Path $srcRoot 'external\ngx'
    $ngxLibDest = Join-Path $ngxDest 'libs'
    New-Item -ItemType Directory -Path $ngxDest -Force | Out-Null
    New-Item -ItemType Directory -Path $ngxLibDest -Force | Out-Null
    $headers = @(Get-ChildItem -LiteralPath (Join-Path $nvidiaRoot 'include') -Recurse -File -Filter 'nvsdk_ngx*.h')
    if ($headers.Count -eq 0) { Fail 'No nvsdk_ngx*.h headers found in local SDK.' }
    foreach ($h in $headers) { Copy-Item -LiteralPath $h.FullName -Destination (Join-Path $ngxDest $h.Name) -Force }
    $ngxLib = Join-Path $nvidiaRoot 'lib\Windows_x86_64\x64\nvsdk_ngx_d.lib'
    if (-not (Test-Path -LiteralPath $ngxLib)) { Fail 'nvsdk_ngx_d.lib missing from local SDK.' }
    Copy-Item -LiteralPath $ngxLib -Destination (Join-Path $ngxLibDest 'nvsdk_ngx_d.lib') -Force

    # ---- Vulkan headers exactly like upstream build workflow
    Say 'Fetching Vulkan headers needed by the full feeder addon build...'
    $vkZip = Join-Path $Work 'vulkan.zip'
    $vkExtract = Join-Path $Work 'vulkan'
    if ($LocalVulkanZip) { Copy-Item -LiteralPath $LocalVulkanZip -Destination $vkZip }
    else { Invoke-WebRequest -UseBasicParsing -TimeoutSec 90 -Uri 'https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/heads/main.zip' -OutFile $vkZip }
    Expand-Archive -LiteralPath $vkZip -DestinationPath $vkExtract -Force
    $vkRoots = @(Get-ChildItem -LiteralPath $vkExtract -Directory | Where-Object { $_.Name -like 'Vulkan-Headers-*' })
    if ($vkRoots.Count -ne 1) { Fail 'Could not identify Vulkan-Headers root.' }
    $vkRoot = $vkRoots[0].FullName
    $vkDest = Join-Path $srcRoot 'external\vulkan'
    New-Item -ItemType Directory -Path (Join-Path $vkDest 'vulkan') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $vkDest 'vk_video') -Force | Out-Null
    Copy-Item -Path (Join-Path $vkRoot 'include\vulkan\*.h') -Destination (Join-Path $vkDest 'vulkan') -Force
    Copy-Item -Path (Join-Path $vkRoot 'include\vk_video\*.h') -Destination (Join-Path $vkDest 'vk_video') -Force

    # ---- Patch source
    Say 'Patching D3D11 VR path for packed foveated stereo...'
    $src = Get-Content -LiteralPath $cppPath -Raw
    # ZIP archives can carry LF or CRLF; literal anchors use this script's CRLF.
    $src = $src.Replace("`r`n", "`n").Replace("`n", "`r`n")

    # v26.12: history is unsafe while both pose and geometry guides are absent.
    $probeState = @'
struct VrCurrentInputScope { void *cmd; void *input; void *output; };
static thread_local VrCurrentInputScope g_vr_current_input = {};
extern "C" __declspec(dllexport) bool __cdecl DLSS5UseCurrentVRInput(void *cmd, void *input, void *output)
{
    return cmd && input && output && g_vr_current_input.cmd == cmd &&
        g_vr_current_input.input == input && g_vr_current_input.output == output;
}
static bool g_vr_depth_guides_reliable = false;
static void *g_vr_guide_owner = nullptr;
static UINT g_vr_guide_width = 0, g_vr_guide_height = 0;
static ULONGLONG g_vr_guide_checked = 0;
static float g_vr_neural_weight = 1.0f;
static void *g_vr_probe_owner = nullptr;
static UINT g_vr_probe_width = 0, g_vr_probe_height = 0;
static double g_mv_probe_mean_px;
static bool VrGuideProbeDue(ULONGLONG now)
{
    static ULONGLONG last = 0;
    static bool started = false;
    if (started && now >= last && now - last < 250) return false;
    last = now;
    started = true;
    return true;
}
'@
    $src = Replace-LiteralOnce $src 'static double g_mv_probe_mean_px;' $probeState 'VR history probe state'
    $src = Replace-LiteralOnce $src 'static const UINT kGuideProbeEvery      = 600;' 'static const UINT kGuideProbeEvery      = 60;' 'VR guide refresh interval'
    $probeValidity = @'
    const bool flat = finite == 0 || max_depth - min_depth < 1e-6;
    g_vr_depth_guides_reliable = !flat && finite >= total * 95 / 100 &&
        min_depth >= 0.0 && max_depth <= 1.0;
    g_vr_guide_owner = g_vr_probe_owner;
    g_vr_guide_width = g_vr_probe_width;
    g_vr_guide_height = g_vr_probe_height;
    g_vr_guide_checked = GetTickCount64();
'@
    $src = Replace-LiteralOnce $src '    const bool flat = finite == 0 || max_depth - min_depth < 1e-6;' $probeValidity 'VR depth reliability'
    $probeCapture = @'
    if (!VrGuideProbeDue(GetTickCount64())) return;
    g_vr_probe_owner = g.runtime;
    g_vr_probe_width = g.width;
    g_vr_probe_height = g.height;
'@
    $src = Replace-LiteralOnce $src '    if ((g_guide_probe_frames % kGuideProbeEvery) != 0) return;' $probeCapture 'VR probe owner'

    # Mark the binary clearly.
    $src = Replace-LiteralOnce $src '#define FEED_VERSION "1.16.0-beta.4"' '#define FEED_VERSION "1.16.0-beta.4-vr-universal-foveated26.28"' 'version marker'

    # Insert the helpers immediately before the real function. This is deliberately
    # independent of comments/newline style around the function.
    $helperCode = @"
// ---------------------------------------------------------------------------
// D3D11 VR box-foveated work preparation and feathered copy-home
// ---------------------------------------------------------------------------

// Runtime-configurable current-frame foveation. This deliberately uses a broad
// packed side-by-side buffer policy instead of one headset-specific resolution.
// Desktop/mirror rejection is handled by the no-window VR runtime gate.
static int g_vr_foveation_enabled = 1;
static int g_vr_foveation_preset = 2; // 0 custom, 1 small, 2 balanced, 3 wide, 4 large
static int g_vr_foveation_width = 60;
static int g_vr_foveation_height = 50;
static ULONGLONG g_vr_foveation_cfg_checked = 0;

static void VrFoveationCfgPath(char *out)
{
    GetModuleFileNameA(g_self, out, MAX_PATH);
    if (char *p = strrchr(out, '\\'))
        strcpy_s(p + 1, MAX_PATH - (p + 1 - out), "dlss5-feed.cfg");
}

static void VrFoveationClamp()
{
    if (g_vr_foveation_enabled != 0) g_vr_foveation_enabled = 1;
    if (g_vr_foveation_preset < 0 || g_vr_foveation_preset > 4) g_vr_foveation_preset = 2;
    if (g_vr_foveation_width < 35) g_vr_foveation_width = 35;
    if (g_vr_foveation_width > 90) g_vr_foveation_width = 90;
    if (g_vr_foveation_height < 35) g_vr_foveation_height = 35;
    if (g_vr_foveation_height > 90) g_vr_foveation_height = 90;
}

static void VrFoveationCfgLoad(bool force = false)
{
    const ULONGLONG now = GetTickCount64();
    if (!force && g_vr_foveation_cfg_checked != 0 && now - g_vr_foveation_cfg_checked < 750)
        return;
    g_vr_foveation_cfg_checked = now;

    char path[MAX_PATH]; VrFoveationCfgPath(path);
    FILE *f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || f == nullptr) return;
    char line[192];
    while (fgets(line, sizeof(line), f) != nullptr)
    {
        char key[96] = {}; float value = 0.0f;
        if (sscanf_s(line, "%95[^=]=%f", key, static_cast<unsigned>(sizeof(key)), &value) != 2) continue;
        const int iv = static_cast<int>(value);
        if      (_stricmp(key, "vr_foveation") == 0)        g_vr_foveation_enabled = iv;
        else if (_stricmp(key, "vr_foveation_preset") == 0) g_vr_foveation_preset = iv;
        else if (_stricmp(key, "vr_foveation_width") == 0)  g_vr_foveation_width = iv;
        else if (_stricmp(key, "vr_foveation_height") == 0) g_vr_foveation_height = iv;
    }
    fclose(f);
    VrFoveationClamp();
}

static void VrFoveationCfgSave()
{
    char path[MAX_PATH]; VrFoveationCfgPath(path);
    std::string kept;
    FILE *r = nullptr;
    if (fopen_s(&r, path, "r") == 0 && r != nullptr)
    {
        char line[256];
        while (fgets(line, sizeof(line), r) != nullptr)
        {
            char key[96] = {};
            if (sscanf_s(line, "%95[^=]", key, static_cast<unsigned>(sizeof(key))) == 1 &&
                (_stricmp(key, "vr_foveation") == 0 || _stricmp(key, "vr_foveation_preset") == 0 ||
                 _stricmp(key, "vr_foveation_width") == 0 || _stricmp(key, "vr_foveation_height") == 0))
                continue;
            kept += line;
            if (!kept.empty() && kept.back() != '\n') kept += '\n';
        }
        fclose(r);
    }
    FILE *f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || f == nullptr) return;
    if (!kept.empty()) fputs(kept.c_str(), f);
    fprintf(f, "vr_foveation=%d\nvr_foveation_preset=%d\nvr_foveation_width=%d\nvr_foveation_height=%d\n",
            g_vr_foveation_enabled, g_vr_foveation_preset, g_vr_foveation_width, g_vr_foveation_height);
    fclose(f);
    g_vr_foveation_cfg_checked = GetTickCount64();
}

static void VrFoveationPercent(UINT *width_percent, UINT *height_percent)
{
    VrFoveationCfgLoad();
    UINT w = static_cast<UINT>(g_vr_foveation_width);
    UINT h = static_cast<UINT>(g_vr_foveation_height);
    switch (g_vr_foveation_preset)
    {
        case 1: w = 50; h = 45; break; // Small: 22.5%
        case 2: w = 60; h = 50; break; // Balanced: 30% (v25 working target)
        case 3: w = 70; h = 50; break; // Wide: 35%
        case 4: w = 75; h = 60; break; // Large: 45%
        default: break;
    }
    if (width_percent) *width_percent = w;
    if (height_percent) *height_percent = h;
}

static bool VrPackedSbsCandidate(UINT source_w, UINT source_h)
{
    // D3D11 texture2D limit is 16384. Keep only structural SBS requirements here;
    // the headless ReShade runtime gate prevents a normal desktop mirror from entering.
    if (source_w < 768u || source_w > 16384u || source_h < 384u || source_h > 16384u || (source_w & 1u))
        return false;
    const UINT eye_w = source_w / 2u;
    if (eye_w < 384u) return false;
    const float aspect = static_cast<float>(eye_w) / static_cast<float>(source_h);
    return aspect >= 0.30f && aspect <= 3.20f;
}

static bool VrFoveatedLayout(UINT source_w, UINT source_h,
                             UINT *crop_w, UINT *crop_h,
                             UINT *work_w, UINT *work_h)
{
    if (crop_w == nullptr || crop_h == nullptr || work_w == nullptr || work_h == nullptr)
        return false;
    VrFoveationCfgLoad();
    if (!g_vr_foveation_enabled || !VrPackedSbsCandidate(source_w, source_h))
        return false;

    UINT wp = 60u, hp = 50u; VrFoveationPercent(&wp, &hp);
    const UINT eye_w = source_w / 2u;
    UINT cw = (eye_w * wp) / 100u;
    UINT ch = (source_h * hp) / 100u;
    cw &= ~1u; ch &= ~1u;
    if (cw < 192u || ch < 192u || cw + 16u >= eye_w || ch + 16u >= source_h || cw > 8192u)
        return false;
    if (cw * 2u > 16384u) return false;

    *crop_w = cw; *crop_h = ch; *work_w = cw * 2u; *work_h = ch;
    return true;
}

// Copy compatibility follows DXGI type groups, never bytes-per-pixel alone.
static int VrCopyFormatFamily(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return 1;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return 2;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return 3;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM: return 4;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return 5;
    case DXGI_FORMAT_R16G16B16A16_UNORM: return 6;
    // UNORM and FLOAT share a storage family but have different numeric meaning.
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return 7;
    default: return 0;
    }
}

static bool VrCopyFormatsCompatible(DXGI_FORMAT a, DXGI_FORMAT b)
{
    return (a != DXGI_FORMAT_UNKNOWN && a == b) ||
        (VrCopyFormatFamily(a) != 0 && VrCopyFormatFamily(a) == VrCopyFormatFamily(b));
}

static bool VrFoveatedPackInputs(ID3D11DeviceContext *ctx,
                                 ID3D11Texture2D *color, ID3D11Texture2D *mv,
                                 ID3D11Texture2D *depth, ID3D11Texture2D *mask,
                                 UINT source_w, UINT source_h)
{
    UINT crop_w = 0, crop_h = 0, work_w = 0, work_h = 0;
    if (!VrFoveatedLayout(source_w, source_h, &crop_w, &crop_h, &work_w, &work_h) ||
        g.width != work_w || g.height != work_h)
        return false;

    if (g.pq_bridge)
    {
        static bool said = false;
        if (!said) { said = true; Log("[feed] VR box foveated path: HDR10 PQ bridge is not supported; leaving frame native"); }
        return false;
    }

    D3D11_TEXTURE2D_DESC src_cd = {}, dst_cd = {}, out_cd = {};
    color->GetDesc(&src_cd);
    g.tex11[SLOT_COLOR]->GetDesc(&dst_cd);
    g.tex11[SLOT_OUTPUT]->GetDesc(&out_cd);
    if (src_cd.Format != dst_cd.Format || src_cd.Format != out_cd.Format)
    {
        static bool said = false;
        if (!said)
        {
            said = true;
            Log("[feed] VR foveated path: copy formats differ (backbuffer=%s color=%s output=%s); leaving frame native",
                FormatName(src_cd.Format), FormatName(dst_cd.Format), FormatName(out_cd.Format));
        }
        return false;
    }

    auto valid_input = [&](ID3D11Texture2D *src, ID3D11Texture2D *dst) {
        if (src == nullptr || dst == nullptr) return false;
        D3D11_TEXTURE2D_DESC a = {}, b = {};
        src->GetDesc(&a); dst->GetDesc(&b);
        return a.Width == source_w && a.Height == source_h &&
            b.Width == work_w && b.Height == work_h &&
            a.ArraySize == 1 && b.ArraySize == 1 &&
            a.SampleDesc.Count == 1 && b.SampleDesc.Count == 1 &&
            VrCopyFormatsCompatible(a.Format, b.Format);
    };
    if (!valid_input(color, g.tex11[SLOT_COLOR]) ||
        !valid_input(mv, g.tex11[SLOT_MV]) ||
        !valid_input(depth, g.tex11[SLOT_DEPTH]) ||
        (g.mask_ok && mask != nullptr && !valid_input(mask, g.tex11[SLOT_MASK])))
    {
        static bool said = false;
        if (!said) { said = true; Log("[feed] VR foveation rejected incompatible guide dimensions, format, array or MSAA; frame left native"); }
        return false;
    }

    const UINT eye_w = source_w / 2u;
    const UINT inset_x = (eye_w - crop_w) / 2u;
    const UINT inset_y = (source_h - crop_h) / 2u;

    D3D11_BOX left = {};
    left.left = inset_x;
    left.right = inset_x + crop_w;
    left.top = inset_y;
    left.bottom = inset_y + crop_h;
    left.front = 0;
    left.back = 1;

    D3D11_BOX right = left;
    right.left += eye_w;
    right.right += eye_w;

    auto copy_pair = [&](ID3D11Texture2D *dst, ID3D11Texture2D *src)
    {
        ctx->CopySubresourceRegion(dst, 0, 0,      0, 0, src, 0, &left);
        ctx->CopySubresourceRegion(dst, 0, crop_w, 0, 0, src, 0, &right);
    };

    copy_pair(g.tex11[SLOT_COLOR], color);
    copy_pair(g.tex11[SLOT_DEPTH], depth);
    copy_pair(g.tex11[SLOT_MV], mv);

    if (g.mask_ok && mask != nullptr)
        copy_pair(g.tex11[SLOT_MASK], mask);
    else
    {
        const FLOAT zero[4] = {};
        if (g.input_rtv[SLOT_MASK] != nullptr)
            ctx->ClearRenderTargetView(g.input_rtv[SLOT_MASK], zero);
    }

    return true;
}

// Keep this copy helper for the ordinary diagnostic/mode-1 path and for the
// existing v5 format-family safety patch. Normal NR presentation below uses
// the feathered shader instead.
static bool VrFoveatedCopyHome(ID3D11DeviceContext *ctx, ID3D11Texture2D *backbuffer,
                               UINT source_w, UINT source_h)
{
    UINT crop_w = 0, crop_h = 0, work_w = 0, work_h = 0;
    if (!VrFoveatedLayout(source_w, source_h, &crop_w, &crop_h, &work_w, &work_h) ||
        g.width != work_w || g.height != work_h)
        return false;

    D3D11_TEXTURE2D_DESC dst_desc = {}, src_desc = {};
    backbuffer->GetDesc(&dst_desc);
    g.tex11[SLOT_OUTPUT]->GetDesc(&src_desc);
    if (dst_desc.Format != src_desc.Format) return false;

    const UINT eye_w = source_w / 2u;
    const UINT inset_x = (eye_w - crop_w) / 2u;
    const UINT inset_y = (source_h - crop_h) / 2u;

    D3D11_BOX left = {};
    left.left = 0;
    left.right = crop_w;
    left.top = 0;
    left.bottom = crop_h;
    left.front = 0;
    left.back = 1;

    D3D11_BOX right = left;
    right.left = crop_w;
    right.right = work_w;

    ctx->CopySubresourceRegion(backbuffer, 0, inset_x,         inset_y, 0, g.tex11[SLOT_OUTPUT], 0, &left);
    ctx->CopySubresourceRegion(backbuffer, 0, eye_w + inset_x, inset_y, 0, g.tex11[SLOT_OUTPUT], 0, &right);
    return true;
}

// Soft blend directly from the completed packed NR output.
// IMPORTANT: this does NOT retain an old NR frame between updates. That cached
// stale frame was what made v3 visibly shake during head motion.
struct VrFeatherState
{
    ID3D11Device *device;
    ID3D11PixelShader *ps;
    ID3D11BlendState *blend;
    ID3D11Buffer *cb;
};
static VrFeatherState g_vr_feather = {};

static void VrFeatherRelease()
{
    SafeRelease(g_vr_feather.ps);
    SafeRelease(g_vr_feather.blend);
    SafeRelease(g_vr_feather.cb);
    g_vr_feather = {};
}

static bool VrFeatherEnsure()
{
    if (g.dev11 == nullptr || g.blit_vs == nullptr || g.blit_sampler == nullptr || g.output_srv == nullptr)
        return false;

    if (g_vr_feather.device == g.dev11 && g_vr_feather.ps != nullptr && g_vr_feather.blend != nullptr && g_vr_feather.cb != nullptr)
        return true;

    VrFeatherRelease();
    g_vr_feather.device = g.dev11;

    static const char kBlendSrc[] =
        "Texture2D<float4> src_color : register(t0);\n"
        "SamplerState linear_smp : register(s0);\n"
        "cbuffer VrFovea : register(b0) { float4 fovea; float4 presentation; };\n"
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
        "float4 ps_vr_feather(VSOut i) : SV_Target {\n"
        "  const float cropX = fovea.x;\n"
        "  const float cropY = fovea.y;\n"
        "  float rightEye = i.uv.x >= 0.5 ? 1.0 : 0.0;\n"
        "  float eyeU = frac(i.uv.x * 2.0);\n"
        "  float startX = fovea.z;\n"
        "  float startY = fovea.w;\n"
        "  float localU = (eyeU - startX) / cropX;\n"
        "  float localV = (i.uv.y - startY) / cropY;\n"
        "  if (localU <= 0.0 || localU >= 1.0 || localV <= 0.0 || localV >= 1.0)\n"
        "    return float4(0,0,0,0);\n"
        "  float edgeU = abs(localU - 0.5) * 2.0;\n"
        "  float edgeV = abs(localV - 0.5) * 2.0;\n"
        "  float edge = max(edgeU, edgeV);\n"
        "  float a = 1.0 - smoothstep(0.62, 0.99, edge);\n"
        "  float srcU = rightEye * 0.5 + saturate(localU) * 0.5;\n"
        "  float3 c = src_color.SampleLevel(linear_smp, float2(srcU, saturate(localV)), 0).rgb;\n"
        "  return float4(c, a * saturate(presentation.x));\n"
        "}\n";

    HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");
    auto compile = m != nullptr ? reinterpret_cast<pD3DCompile>(GetProcAddress(m, "D3DCompile")) : nullptr;
    if (compile == nullptr) return false;

    ID3DBlob *blob = nullptr, *err = nullptr;
    HRESULT hr = compile(kBlendSrc, sizeof(kBlendSrc) - 1, "vrboxfeather", nullptr, nullptr,
                         "ps_vr_feather", "ps_4_0", 0, 0, &blob, &err);
    if (FAILED(hr))
    {
        Log("[feed] VR box feather PS compile failed 0x%08X: %s", hr,
            err ? (const char *)err->GetBufferPointer() : "");
        SafeRelease(err); SafeRelease(blob);
        VrFeatherRelease();
        return false;
    }
    SafeRelease(err);

    hr = g.dev11->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_vr_feather.ps);
    SafeRelease(blob);
    if (FAILED(hr))
    {
        VrFeatherRelease();
        return false;
    }

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(g.dev11->CreateBlendState(&bd, &g_vr_feather.blend)))
    {
        VrFeatherRelease();
        return false;
    }

    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth = 32u;
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g.dev11->CreateBuffer(&cbd, nullptr, &g_vr_feather.cb)))
    {
        VrFeatherRelease();
        return false;
    }

    UINT wp = 60u, hp = 50u; VrFoveationPercent(&wp, &hp);
    Log("[feed] VR CURRENT-FRAME FEATHER ready: width=%u%% height=%u%% (%u.%u%% pixel budget), four-edge blend",
        wp, hp, (wp * hp) / 100u, (wp * hp) % 100u);
    return true;
}

static bool VrFoveatedFeatherHome(ID3D11DeviceContext *ctx, ID3D11RenderTargetView *rtv,
                                  UINT source_w, UINT source_h)
{
    UINT crop_w = 0, crop_h = 0, work_w = 0, work_h = 0;
    if (!VrFoveatedLayout(source_w, source_h, &crop_w, &crop_h, &work_w, &work_h) ||
        g.width != work_w || g.height != work_h || !VrFeatherEnsure())
        return false;

    UINT wp = 60u, hp = 50u; VrFoveationPercent(&wp, &hp);
    const UINT eye_w = source_w / 2u;
    const float fovea_cb[8] = {
        static_cast<float>(crop_w) / eye_w, static_cast<float>(crop_h) / source_h,
        static_cast<float>((eye_w - crop_w) / 2u) / eye_w,
        static_cast<float>((source_h - crop_h) / 2u) / source_h,
        g_vr_neural_weight, 0.0f, 0.0f, 0.0f };
    D3D11_MAPPED_SUBRESOURCE vrfm = {};
    if (FAILED(ctx->Map(g_vr_feather.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &vrfm))) return false;
    memcpy(vrfm.pData, fovea_cb, sizeof(fovea_cb));
    ctx->Unmap(g_vr_feather.cb, 0);


    ID3D11RenderTargetView   *old_rtv = nullptr;
    ID3D11DepthStencilView   *old_dsv = nullptr;
    ID3D11VertexShader       *old_vs  = nullptr;
    ID3D11PixelShader        *old_ps  = nullptr;
    ID3D11ShaderResourceView *old_srv = nullptr;
    ID3D11SamplerState       *old_smp = nullptr;
    ID3D11Buffer             *old_cb  = nullptr;
    ID3D11InputLayout        *old_il  = nullptr;
    ID3D11BlendState         *old_bs  = nullptr; FLOAT old_bf[4] = {}; UINT old_mask = 0;
    ID3D11DepthStencilState  *old_ds  = nullptr; UINT old_sref = 0;
    ID3D11RasterizerState    *old_rs  = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY old_topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    UINT nvp = 1; D3D11_VIEWPORT old_vp = {};

    ctx->OMGetRenderTargets(1, &old_rtv, &old_dsv);
    ctx->VSGetShader(&old_vs, nullptr, nullptr);
    ctx->PSGetShader(&old_ps, nullptr, nullptr);
    ctx->PSGetShaderResources(0, 1, &old_srv);
    ctx->PSGetSamplers(0, 1, &old_smp);
    ctx->PSGetConstantBuffers(0, 1, &old_cb);
    ctx->IAGetInputLayout(&old_il);
    ctx->IAGetPrimitiveTopology(&old_topo);
    ctx->OMGetBlendState(&old_bs, old_bf, &old_mask);
    ctx->OMGetDepthStencilState(&old_ds, &old_sref);
    ctx->RSGetState(&old_rs);
    ctx->RSGetViewports(&nvp, &old_vp);

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(source_w);
    vp.Height = static_cast<float>(source_h);
    vp.MaxDepth = 1.0f;


    ID3D11RenderTargetView *target[] = { rtv };
    ID3D11ShaderResourceView *srv[] = { g.output_srv };
    ID3D11SamplerState *smp[] = { g.blit_sampler };
    const FLOAT blend_factor[4] = {};

    ctx->OMSetRenderTargets(1, target, nullptr);
    ctx->OMSetBlendState(g_vr_feather.blend, blend_factor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->RSSetState(nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(g.blit_vs, nullptr, 0);
    ctx->PSSetShader(g_vr_feather.ps, nullptr, 0);
    ctx->PSSetSamplers(0, 1, smp);
    ctx->PSSetConstantBuffers(0, 1, &g_vr_feather.cb);
    ctx->PSSetShaderResources(0, 1, srv);
    ctx->Draw(3, 0);
    static bool presented = false;
    if (!presented) {
        presented = true;
        Log("[feed] VR FOVEATION COPY-HOME ACTIVE: current-frame output composited into both eyes");
    }

    ID3D11ShaderResourceView *null_srv = nullptr;
    ctx->PSSetShaderResources(0, 1, &null_srv);
    ctx->OMSetRenderTargets(1, &old_rtv, old_dsv);
    ctx->VSSetShader(old_vs, nullptr, 0);
    ctx->PSSetShader(old_ps, nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &old_srv);
    ctx->PSSetSamplers(0, 1, &old_smp);
    ctx->PSSetConstantBuffers(0, 1, &old_cb);
    ctx->IASetInputLayout(old_il);
    ctx->IASetPrimitiveTopology(old_topo);
    ctx->OMSetBlendState(old_bs, old_bf, old_mask);
    ctx->OMSetDepthStencilState(old_ds, old_sref);
    ctx->RSSetState(old_rs);
    if (nvp) ctx->RSSetViewports(1, &old_vp);

    SafeRelease(old_rtv); SafeRelease(old_dsv); SafeRelease(old_vs); SafeRelease(old_ps);
    SafeRelease(old_srv); SafeRelease(old_smp); SafeRelease(old_cb); SafeRelease(old_il); SafeRelease(old_bs);
    SafeRelease(old_ds); SafeRelease(old_rs);
    return true;
}

// ---------------------------------------------------------------------------
// Async NR residual compositor.
//
// Do NOT retain the previous processed picture itself: that made head movement
// look shaky. Retain only (NR result - the native source that produced it), and
// add that correction to each CURRENT native frame until the next NR job finishes.
// ---------------------------------------------------------------------------
struct VrMat3
{
    float m[9];
};

static VrMat3 VrMatIdentity()
{
    VrMat3 r = {};
    r.m[0] = r.m[4] = r.m[8] = 1.0f;
    return r;
}

static VrMat3 VrMatMul(const VrMat3 &a, const VrMat3 &b)
{
    VrMat3 r = {};
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            for (int k = 0; k < 3; ++k)
                r.m[y * 3 + x] += a.m[y * 3 + k] * b.m[k * 3 + x];
    return r;
}

struct VrPoseUniformState
{
    reshade::api::effect_runtime *rt;
    reshade::api::effect_uniform_variable valid;
    reshade::api::effect_uniform_variable r0;
    reshade::api::effect_uniform_variable r1;
    reshade::api::effect_uniform_variable r2;
    reshade::api::effect_uniform_variable proj_l;
    reshade::api::effect_uniform_variable proj_r;
};
static VrPoseUniformState g_vr_pose_uniforms = {};

struct VrPoseCarryState
{
    VrMat3 pending_current_to_source;
    VrMat3 display_current_to_source;

    float current_proj_l[4];
    float current_proj_r[4];
    float pending_source_proj_l[4];
    float pending_source_proj_r[4];
    float display_source_proj_l[4];
    float display_source_proj_r[4];

    bool current_valid;
    bool pending_valid;
    bool display_valid;
    UINT pending_steps;
};
static VrPoseCarryState g_vr_pose_carry = {};

static void VrCopy4(float *dst, const float *src)
{
    for (int i = 0; i < 4; ++i) dst[i] = src[i];
}

static bool VrReadPoseStep(reshade::api::effect_runtime *rt,
                           VrMat3 *step, float *proj_l, float *proj_r)
{
    if (rt == nullptr || step == nullptr || proj_l == nullptr || proj_r == nullptr)
        return false;

    if (g_vr_pose_uniforms.rt != rt)
    {
        g_vr_pose_uniforms = {};
        g_vr_pose_uniforms.rt = rt;
        g_vr_pose_uniforms.valid  = rt->find_uniform_variable("DLSS5_Feed.fx", "VR_POSE_VALID");
        g_vr_pose_uniforms.r0     = rt->find_uniform_variable("DLSS5_Feed.fx", "VR_POSE_R0");
        g_vr_pose_uniforms.r1     = rt->find_uniform_variable("DLSS5_Feed.fx", "VR_POSE_R1");
        g_vr_pose_uniforms.r2     = rt->find_uniform_variable("DLSS5_Feed.fx", "VR_POSE_R2");
        g_vr_pose_uniforms.proj_l = rt->find_uniform_variable("DLSS5_Feed.fx", "VR_POSE_PROJ_L");
        g_vr_pose_uniforms.proj_r = rt->find_uniform_variable("DLSS5_Feed.fx", "VR_POSE_PROJ_R");
    }

    if (g_vr_pose_uniforms.valid.handle == 0 ||
        g_vr_pose_uniforms.r0.handle == 0 ||
        g_vr_pose_uniforms.r1.handle == 0 ||
        g_vr_pose_uniforms.r2.handle == 0 ||
        g_vr_pose_uniforms.proj_l.handle == 0 ||
        g_vr_pose_uniforms.proj_r.handle == 0)
        return false;

    float valid = 0.0f;
    float r0[4] = {}, r1[4] = {}, r2[4] = {};
    rt->get_uniform_value_float(g_vr_pose_uniforms.valid, &valid, 1);
    rt->get_uniform_value_float(g_vr_pose_uniforms.r0, r0, 4);
    rt->get_uniform_value_float(g_vr_pose_uniforms.r1, r1, 4);
    rt->get_uniform_value_float(g_vr_pose_uniforms.r2, r2, 4);
    rt->get_uniform_value_float(g_vr_pose_uniforms.proj_l, proj_l, 4);
    rt->get_uniform_value_float(g_vr_pose_uniforms.proj_r, proj_r, 4);

    if (valid <= 0.5f)
        return false;

    step->m[0] = r0[0]; step->m[1] = r0[1]; step->m[2] = r0[2];
    step->m[3] = r1[0]; step->m[4] = r1[1]; step->m[5] = r1[2];
    step->m[6] = r2[0]; step->m[7] = r2[1]; step->m[8] = r2[2];
    return true;
}

static bool VrHasPoseGuide(reshade::api::effect_runtime *rt)
{
    float valid = 0.0f;
    const auto uniform = rt->find_uniform_variable("DLSS5_Feed.fx", "VR_POSE_VALID");
    if (uniform.handle != 0) rt->get_uniform_value_float(uniform, &valid, 1);
    return std::isfinite(valid) && valid > 0.5f;
}

static float VrMotionScale(reshade::api::effect_runtime *rt, float estimated_scale)
{
    return VrHasPoseGuide(rt) ? 1.0f : estimated_scale;
}

static bool VrHasStereoDepthGuide(reshade::api::effect_runtime *rt)
{
    float valid = 0.0f;
    const auto uniform = rt->find_uniform_variable("DLSS5_Feed.fx", "VR_STEREO_DEPTH_VALID");
    if (uniform.handle != 0) rt->get_uniform_value_float(uniform, &valid, 1);
    return std::isfinite(valid) && valid > 0.5f;
}

// A rotation-only guide cannot establish history correspondence at finite depth.
// Use the real NGX reset contract during motion when geometry is unavailable;
// the bias-current mask alone is only a soft hint to the neural consumer.
static bool VrUnresolvedPoseMotion(reshade::api::effect_runtime *rt)
{
    float translation = 0.0f;
    auto uniform = rt->find_uniform_variable("DLSS5_Feed.fx", "VR_POSE_TRANSLATION_M");
    if (uniform.handle == 0) return true;
    rt->get_uniform_value_float(uniform, &translation, 1);
    if (!std::isfinite(translation) || std::abs(translation) > 0.0001f) return true;
    for (int eye = 0; eye < 2; ++eye)
        for (int row = 0; row < 3; ++row)
        {
            char name[40];
            std::snprintf(name, sizeof(name), eye ? "VR_POSE_RIGHT_R%d" : "VR_POSE_R%d", row);
            uniform = rt->find_uniform_variable("DLSS5_Feed.fx", name);
            if (uniform.handle == 0) return true;
            float values[4] = {};
            rt->get_uniform_value_float(uniform, values, 4);
            for (int col = 0; col < 3; ++col)
                if (!std::isfinite(values[col]) ||
                    std::abs(values[col] - (row == col ? 1.0f : 0.0f)) * (g.width * 0.5f) > 0.5f)
                    return true;
        }
    return false;
}

// Bias-current is a hint, not a substitute for resetting invalid history.
static bool VrHistoryGuard(reshade::api::effect_runtime *rt)
{
    const bool pose_valid = VrHasPoseGuide(rt);
    const bool depth_valid = VrHasStereoDepthGuide(rt) && g_vr_depth_guides_reliable &&
        g_vr_guide_owner == rt && g_vr_guide_width == g.width &&
        g_vr_guide_height == g.height && GetTickCount64() - g_vr_guide_checked < 1500;
    static reshade::api::effect_runtime *owner = nullptr;
    static unsigned width = 0, height = 0;
    static ULONGLONG settle_until = 0;
    static ULONGLONG last_reset = 0;
    static bool recovering = false;
    const ULONGLONG now = GetTickCount64();
    if (owner != rt || width != g.width || height != g.height)
    {
        owner = rt; width = g.width; height = g.height; settle_until = 0;
        last_reset = now; recovering = true;
    }
    if (!depth_valid && (!pose_valid || VrUnresolvedPoseMotion(rt))) settle_until = now + 120;
    if (depth_valid) settle_until = 0;
    const bool unresolved_motion = !depth_valid && now < settle_until;
    const bool reset = (!pose_valid && !depth_valid) || unresolved_motion;
    // Keep evaluating so the neural history can converge after motion, but do
    // not display unstable reset frames. Never blend pixels from an old frame.
    if (reset) { last_reset = now; recovering = true; }
    if (depth_valid) recovering = false;
    const ULONGLONG elapsed = now - last_reset;
    g_vr_neural_weight = !recovering ? 1.0f :
        (elapsed <= 400 ? 0.0f : (std::min)(1.0f, float(elapsed - 400) / 600.0f));
    if (g_vr_neural_weight >= 1.0f) recovering = false;
    static int last_output = -1;
    const int output = g_vr_neural_weight <= 0.0f ? 0 : (g_vr_neural_weight >= 1.0f ? 2 : 1);
    if (last_output != output)
    {
        last_output = output;
        Log("[feed] VR PRESENTATION: %s (pose=%d depth=%d); neural evaluation continues",
            output == 0 ? "CURRENT NATIVE fallback during unresolved motion" :
            (output == 1 ? "CURRENT-FRAME neural fade-in" : "FULL neural output"), pose_valid, depth_valid);
    }
    static int last = -1;
    if (last != int(reset))
    {
        last = int(reset);
        Log("[feed] VR HISTORY GUARD: %s (pose=%d depth=%d motionWithoutDepth=%d); neural evaluation remains enabled",
            reset ? "RESET unusable temporal correspondence" : "REUSE verified/static guides", pose_valid, depth_valid, unresolved_motion);
    }
    return reset;
}

static void VrPoseReset()
{
    g_vr_pose_carry = {};
    g_vr_pose_carry.pending_current_to_source = VrMatIdentity();
    g_vr_pose_carry.display_current_to_source = VrMatIdentity();
    g_vr_pose_uniforms = {};
}

static void VrPoseAdvance(reshade::api::effect_runtime *rt, bool pending_active)
{
    VrMat3 step = VrMatIdentity();
    float pl[4] = {}, pr[4] = {};
    g_vr_pose_carry.current_valid = VrReadPoseStep(rt, &step, pl, pr);
    if (!g_vr_pose_carry.current_valid)
        return;

    VrCopy4(g_vr_pose_carry.current_proj_l, pl);
    VrCopy4(g_vr_pose_carry.current_proj_r, pr);

    // The bridge matrix maps THIS frame's ray to the PREVIOUS effect frame.
    // If C maps previous -> original source, then C * step maps current -> source.
    if (pending_active && g_vr_pose_carry.pending_valid)
    {
        g_vr_pose_carry.pending_current_to_source =
            VrMatMul(g_vr_pose_carry.pending_current_to_source, step);
        if (g_vr_pose_carry.pending_steps < 1000000u)
            ++g_vr_pose_carry.pending_steps;
    }

    if (g_vr_pose_carry.display_valid)
        g_vr_pose_carry.display_current_to_source =
            VrMatMul(g_vr_pose_carry.display_current_to_source, step);
}

static void VrPoseStartPending()
{
    g_vr_pose_carry.pending_current_to_source = VrMatIdentity();
    g_vr_pose_carry.pending_steps = 0;
    g_vr_pose_carry.pending_valid = g_vr_pose_carry.current_valid;
    if (g_vr_pose_carry.pending_valid)
    {
        VrCopy4(g_vr_pose_carry.pending_source_proj_l, g_vr_pose_carry.current_proj_l);
        VrCopy4(g_vr_pose_carry.pending_source_proj_r, g_vr_pose_carry.current_proj_r);
    }
}

static void VrPosePromotePending()
{
    g_vr_pose_carry.display_current_to_source = g_vr_pose_carry.pending_current_to_source;
    g_vr_pose_carry.display_valid = g_vr_pose_carry.pending_valid;
    if (g_vr_pose_carry.display_valid)
    {
        VrCopy4(g_vr_pose_carry.display_source_proj_l, g_vr_pose_carry.pending_source_proj_l);
        VrCopy4(g_vr_pose_carry.display_source_proj_r, g_vr_pose_carry.pending_source_proj_r);
    }
}


// ---------------------------------------------------------------------------
// Exact async temporal-MV repair.
//
// The original v2.7c pose path generated current DISPLAY frame -> previous DISPLAY
// frame motion. That is correct only while every display frame is also an NR frame.
//
// The async path intentionally skips NR submissions while the previous job is in
// flight. Therefore the next NR input must carry motion from CURRENT NR frame ->
// PREVIOUS NR frame. g_vr_pose_carry.pending_current_to_source is exactly that
// accumulated OpenVR rotation.
//
// This pass overwrites only the packed work MV + trust mask immediately before
// an NR submission. No guessed multiplier and no optical flow are involved.
// ---------------------------------------------------------------------------
struct VrSubmitMvConstants
{
    float pose_r0[4];
    float pose_r1[4];
    float pose_r2[4];
    float current_proj_l[4];
    float current_proj_r[4];
    float source_proj_l[4];
    float source_proj_r[4];
    float geom[4]; // inv work width, inv work height, crop width fraction, crop height fraction
    float crop_start[4]; // start x, start y, unused, unused
};

struct VrSubmitMvState
{
    ID3D11Device *device;
    ID3D11PixelShader *ps;
    ID3D11Buffer *cb;
};
static VrSubmitMvState g_vr_submit_mv = {};

static void VrSubmitMvRelease()
{
    SafeRelease(g_vr_submit_mv.ps);
    SafeRelease(g_vr_submit_mv.cb);
    g_vr_submit_mv = {};
}

static bool VrSubmitMvEnsure()
{
    if (g.dev11 == nullptr || g.blit_vs == nullptr ||
        g.input_rtv[SLOT_MV] == nullptr || g.input_rtv[SLOT_MASK] == nullptr)
        return false;

    if (g_vr_submit_mv.device == g.dev11 &&
        g_vr_submit_mv.ps != nullptr && g_vr_submit_mv.cb != nullptr)
        return true;

    VrSubmitMvRelease();
    g_vr_submit_mv.device = g.dev11;

    static const char kSubmitMvSrc[] =
        "cbuffer TemporalPose : register(b0) {\n"
        "  float4 pose_r0; float4 pose_r1; float4 pose_r2;\n"
        "  float4 current_proj_l; float4 current_proj_r;\n"
        "  float4 source_proj_l; float4 source_proj_r;\n"
        "  float4 geom;\n"
        "  float4 crop_start;\n"
        "};\n"
        "struct Out { float2 mv : SV_Target0; float mask : SV_Target1; };\n"
        "Out ps_submit_mv(float4 pos : SV_Position) {\n"
        "  Out o; o.mv = float2(0,0); o.mask = 1.0;\n"
        "  float2 uv = pos.xy * geom.xy;\n"
        "  float rightEye = uv.x >= 0.5 ? 1.0 : 0.0;\n"
        "  float2 local = float2(uv.x * 2.0 - rightEye, uv.y);\n"
        "  float2 crop = geom.zw; float2 start = crop_start.xy;\n"
        "  float2 euv = start + local * crop;\n"
        "  float4 cp = rightEye > 0.5 ? current_proj_r : current_proj_l;\n"
        "  float4 sp = rightEye > 0.5 ? source_proj_r : source_proj_l;\n"
        "  float tx = lerp(cp.x, cp.y, euv.x);\n"
        "  float ty = lerp(cp.w, cp.z, euv.y);\n"
        "  float3 dcur = float3(tx, ty, -1.0);\n"
        "  float3 dprev = float3(dot(pose_r0.xyz,dcur), dot(pose_r1.xyz,dcur), dot(pose_r2.xyz,dcur));\n"
        "  if (dprev.z >= -1e-4) return o;\n"
        "  float invz = -1.0 / dprev.z;\n"
        "  float ptx = dprev.x * invz;\n"
        "  float pty = dprev.y * invz;\n"
        "  float2 peuv = float2((ptx-sp.x)/max(sp.y-sp.x,1e-6), (pty-sp.w)/min(sp.z-sp.w,-1e-6));\n"
        "  if (any(peuv < 0.0) || any(peuv > 1.0)) return o;\n"
        "  float2 prevLocal = (peuv - start) / crop;\n"
        // This is important for a cropped NR surface: a history sample which left
        // the crop must not wrap/clamp into unrelated NR history (or the other eye).
        "  if (any(prevLocal < 0.0) || any(prevLocal > 1.0)) return o;\n"
        "  float2 prevPacked = float2((prevLocal.x + rightEye) * 0.5, prevLocal.y);\n"
        "  float2 workSize = 1.0 / geom.xy;\n"
        "  o.mv = (prevPacked - uv) * workSize;\n"
        "  o.mask = 0.0;\n"
        "  return o;\n"
        "}\n";

    HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");
    auto compile = m != nullptr ? reinterpret_cast<pD3DCompile>(GetProcAddress(m, "D3DCompile")) : nullptr;
    if (compile == nullptr)
        return false;

    ID3DBlob *blob = nullptr, *err = nullptr;
    HRESULT hr = compile(kSubmitMvSrc, sizeof(kSubmitMvSrc) - 1, "vrtemporalmv",
                         nullptr, nullptr, "ps_submit_mv", "ps_4_0",
                         0, 0, &blob, &err);
    if (FAILED(hr))
    {
        Log("[feed] VR temporal MV PS compile failed 0x%08X: %s", hr,
            err ? (const char *)err->GetBufferPointer() : "");
        SafeRelease(err); SafeRelease(blob);
        VrSubmitMvRelease();
        return false;
    }
    SafeRelease(err);

    hr = g.dev11->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                    nullptr, &g_vr_submit_mv.ps);
    SafeRelease(blob);
    if (FAILED(hr))
    {
        Log("[feed] VR temporal MV PS creation failed 0x%08X", hr);
        VrSubmitMvRelease();
        return false;
    }

    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth = sizeof(VrSubmitMvConstants);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g.dev11->CreateBuffer(&cbd, nullptr, &g_vr_submit_mv.cb)))
    {
        Log("[feed] VR temporal MV constant buffer creation failed");
        VrSubmitMvRelease();
        return false;
    }

    Log("[feed] VR TEMPORAL MV FIX ready: async NR uses accumulated current-NR -> previous-NR OpenVR pose");
    return true;
}

static bool VrWriteSubmissionMv(ID3D11DeviceContext *ctx)
{
    if (ctx == nullptr || !g_vr_pose_carry.pending_valid ||
        !g_vr_pose_carry.current_valid || g_vr_pose_carry.pending_steps == 0 ||
        !VrSubmitMvEnsure())
        return false;

    VrSubmitMvConstants c = {};
    c.pose_r0[0] = g_vr_pose_carry.pending_current_to_source.m[0];
    c.pose_r0[1] = g_vr_pose_carry.pending_current_to_source.m[1];
    c.pose_r0[2] = g_vr_pose_carry.pending_current_to_source.m[2];
    c.pose_r1[0] = g_vr_pose_carry.pending_current_to_source.m[3];
    c.pose_r1[1] = g_vr_pose_carry.pending_current_to_source.m[4];
    c.pose_r1[2] = g_vr_pose_carry.pending_current_to_source.m[5];
    c.pose_r2[0] = g_vr_pose_carry.pending_current_to_source.m[6];
    c.pose_r2[1] = g_vr_pose_carry.pending_current_to_source.m[7];
    c.pose_r2[2] = g_vr_pose_carry.pending_current_to_source.m[8];

    VrCopy4(c.current_proj_l, g_vr_pose_carry.current_proj_l);
    VrCopy4(c.current_proj_r, g_vr_pose_carry.current_proj_r);
    VrCopy4(c.source_proj_l, g_vr_pose_carry.pending_source_proj_l);
    VrCopy4(c.source_proj_r, g_vr_pose_carry.pending_source_proj_r);

    c.geom[0] = 1.0f / static_cast<float>(g.width);
    c.geom[1] = 1.0f / static_cast<float>(g.height);
    UINT vr_mv_wp = 60u, vr_mv_hp = 50u; VrFoveationPercent(&vr_mv_wp, &vr_mv_hp);
    c.geom[2] = static_cast<float>(vr_mv_wp) / 100.0f;
    c.geom[3] = static_cast<float>(vr_mv_hp) / 100.0f;
    c.crop_start[0] = 0.5f - 0.5f * c.geom[2];
    c.crop_start[1] = 0.5f - 0.5f * c.geom[3];
    c.crop_start[2] = 0.0f;
    c.crop_start[3] = 0.0f;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(ctx->Map(g_vr_submit_mv.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return false;
    memcpy(mapped.pData, &c, sizeof(c));
    ctx->Unmap(g_vr_submit_mv.cb, 0);

    ID3D11RenderTargetView *old_rtvs[2] = {};
    ID3D11DepthStencilView *old_dsv = nullptr;
    ID3D11VertexShader *old_vs = nullptr;
    ID3D11PixelShader *old_ps = nullptr;
    ID3D11Buffer *old_cb = nullptr;
    ID3D11InputLayout *old_il = nullptr;
    ID3D11BlendState *old_bs = nullptr; FLOAT old_bf[4] = {}; UINT old_mask = 0;
    ID3D11DepthStencilState *old_ds = nullptr; UINT old_sref = 0;
    ID3D11RasterizerState *old_rs = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY old_topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    UINT nvp = 1; D3D11_VIEWPORT old_vp = {};

    ctx->OMGetRenderTargets(2, old_rtvs, &old_dsv);
    ctx->VSGetShader(&old_vs, nullptr, nullptr);
    ctx->PSGetShader(&old_ps, nullptr, nullptr);
    ctx->PSGetConstantBuffers(0, 1, &old_cb);
    ctx->IAGetInputLayout(&old_il);
    ctx->IAGetPrimitiveTopology(&old_topo);
    ctx->OMGetBlendState(&old_bs, old_bf, &old_mask);
    ctx->OMGetDepthStencilState(&old_ds, &old_sref);
    ctx->RSGetState(&old_rs);
    ctx->RSGetViewports(&nvp, &old_vp);

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(g.width);
    vp.Height = static_cast<float>(g.height);
    vp.MaxDepth = 1.0f;

    ID3D11RenderTargetView *targets[2] = {
        g.input_rtv[SLOT_MV],
        g.input_rtv[SLOT_MASK]
    };

    ctx->OMSetRenderTargets(2, targets, nullptr);
    ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->RSSetState(nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(g.blit_vs, nullptr, 0);
    ctx->PSSetShader(g_vr_submit_mv.ps, nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, &g_vr_submit_mv.cb);
    ctx->Draw(3, 0);

    ctx->OMSetRenderTargets(2, old_rtvs, old_dsv);
    ctx->VSSetShader(old_vs, nullptr, 0);
    ctx->PSSetShader(old_ps, nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, &old_cb);
    ctx->IASetInputLayout(old_il);
    ctx->IASetPrimitiveTopology(old_topo);
    ctx->OMSetBlendState(old_bs, old_bf, old_mask);
    ctx->OMSetDepthStencilState(old_ds, old_sref);
    ctx->RSSetState(old_rs);
    if (nvp) ctx->RSSetViewports(1, &old_vp);

    SafeRelease(old_rtvs[0]); SafeRelease(old_rtvs[1]); SafeRelease(old_dsv);
    SafeRelease(old_vs); SafeRelease(old_ps); SafeRelease(old_cb);
    SafeRelease(old_il); SafeRelease(old_bs); SafeRelease(old_ds); SafeRelease(old_rs);

    static UINT logged = 0;
    if (logged < 12)
    {
        ++logged;
        Log("[feed] VR TEMPORAL MV override: NR history span=%u display-frame pose step(s), work=%ux%u, feeder mv_scale remains %.3f,%.3f",
            g_vr_pose_carry.pending_steps, g.width, g.height,
            g_cfg.mv_scale_x, g_cfg.mv_scale_y);
    }
    return true;
}

struct VrResidualConstants
{
    float pose_r0[4];
    float pose_r1[4];
    float pose_r2[4];
    float current_proj_l[4];
    float current_proj_r[4];
    float source_proj_l[4];
    float source_proj_r[4];
};

struct VrResidualState
{
    ID3D11Device *device;

    ID3D11Texture2D *current_tex;
    ID3D11ShaderResourceView *current_srv;

    ID3D11Texture2D *pending_src;
    ID3D11Texture2D *display_src;
    ID3D11ShaderResourceView *display_src_srv;

    ID3D11Texture2D *display_nr;
    ID3D11ShaderResourceView *display_nr_srv;

    ID3D11PixelShader *ps;
    ID3D11Buffer *pose_cb;

    UINT full_w, full_h;
    UINT work_w, work_h;
    DXGI_FORMAT full_fmt;
    DXGI_FORMAT work_fmt;

    bool pending_valid;
    bool display_valid;
};
static VrResidualState g_vr_residual = {};

static void VrResidualRelease()
{
    SafeRelease(g_vr_residual.current_tex);
    SafeRelease(g_vr_residual.current_srv);
    SafeRelease(g_vr_residual.pending_src);
    SafeRelease(g_vr_residual.display_src);
    SafeRelease(g_vr_residual.display_src_srv);
    SafeRelease(g_vr_residual.display_nr);
    SafeRelease(g_vr_residual.display_nr_srv);
    SafeRelease(g_vr_residual.ps);
    SafeRelease(g_vr_residual.pose_cb);
    g_vr_residual = {};
}

static void VrResidualInvalidate()
{
    g_vr_residual.pending_valid = false;
    g_vr_residual.display_valid = false;
    g_vr_pose_carry.display_valid = false;
    g_vr_pose_carry.pending_valid = false;
}

static bool VrResidualEnsure(ID3D11Texture2D *backbuffer, UINT source_w, UINT source_h)
{
    if (g.dev11 == nullptr || backbuffer == nullptr || g.tex11[SLOT_COLOR] == nullptr ||
        g.tex11[SLOT_OUTPUT] == nullptr || g.blit_vs == nullptr || g.blit_sampler == nullptr)
        return false;

    D3D11_TEXTURE2D_DESC bd = {}, cd = {}, od = {};
    backbuffer->GetDesc(&bd);
    g.tex11[SLOT_COLOR]->GetDesc(&cd);
    g.tex11[SLOT_OUTPUT]->GetDesc(&od);

    const DXGI_FORMAT full_typed = TypedColorFormat(bd.Format);
    if (full_typed == DXGI_FORMAT_UNKNOWN || cd.Format != od.Format)
        return false;

    if (g_vr_residual.device == g.dev11 &&
        g_vr_residual.current_tex != nullptr &&
        g_vr_residual.pending_src != nullptr &&
        g_vr_residual.display_src != nullptr &&
        g_vr_residual.display_nr != nullptr &&
        g_vr_residual.ps != nullptr &&
        g_vr_residual.pose_cb != nullptr &&
        g_vr_residual.full_w == source_w &&
        g_vr_residual.full_h == source_h &&
        g_vr_residual.work_w == cd.Width &&
        g_vr_residual.work_h == cd.Height &&
        g_vr_residual.full_fmt == bd.Format &&
        g_vr_residual.work_fmt == cd.Format)
        return true;

    VrResidualRelease();
    g_vr_residual.device = g.dev11;
    g_vr_residual.full_w = source_w;
    g_vr_residual.full_h = source_h;
    g_vr_residual.work_w = cd.Width;
    g_vr_residual.work_h = cd.Height;
    g_vr_residual.full_fmt = bd.Format;
    g_vr_residual.work_fmt = cd.Format;

    D3D11_TEXTURE2D_DESC td = bd;
    td.Format = TypelessColorFormat(bd.Format);
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = 0;
    td.MiscFlags = 0;
    if (FAILED(g.dev11->CreateTexture2D(&td, nullptr, &g_vr_residual.current_tex)))
    {
        Log("[feed] VR pose residual: current-frame staging texture creation failed");
        VrResidualRelease();
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = full_typed;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MostDetailedMip = 0;
    sd.Texture2D.MipLevels = 1;
    if (FAILED(g.dev11->CreateShaderResourceView(g_vr_residual.current_tex, &sd, &g_vr_residual.current_srv)))
    {
        Log("[feed] VR pose residual: current-frame SRV creation failed");
        VrResidualRelease();
        return false;
    }

    D3D11_TEXTURE2D_DESC wd = cd;
    wd.Usage = D3D11_USAGE_DEFAULT;
    wd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    wd.CPUAccessFlags = 0;
    wd.MiscFlags = 0;

    if (FAILED(g.dev11->CreateTexture2D(&wd, nullptr, &g_vr_residual.pending_src)) ||
        FAILED(g.dev11->CreateTexture2D(&wd, nullptr, &g_vr_residual.display_src)) ||
        FAILED(g.dev11->CreateShaderResourceView(g_vr_residual.display_src, nullptr, &g_vr_residual.display_src_srv)))
    {
        Log("[feed] VR pose residual: source snapshot resources failed");
        VrResidualRelease();
        return false;
    }

    D3D11_TEXTURE2D_DESC nd = od;
    nd.Usage = D3D11_USAGE_DEFAULT;
    nd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    nd.CPUAccessFlags = 0;
    nd.MiscFlags = 0;
    if (FAILED(g.dev11->CreateTexture2D(&nd, nullptr, &g_vr_residual.display_nr)) ||
        FAILED(g.dev11->CreateShaderResourceView(g_vr_residual.display_nr, nullptr, &g_vr_residual.display_nr_srv)))
    {
        Log("[feed] VR pose residual: NR snapshot resources failed");
        VrResidualRelease();
        return false;
    }

    static const char kResidualSrc[] =
        "Texture2D<float4> current_frame : register(t0);\n"
        "Texture2D<float4> nr_frame : register(t1);\n"
        "Texture2D<float4> source_frame : register(t2);\n"
        "SamplerState linear_smp : register(s0);\n"
        "cbuffer PoseCarry : register(b0) {\n"
        "  float4 pose_r0; float4 pose_r1; float4 pose_r2;\n"
        "  float4 current_proj_l; float4 current_proj_r;\n"
        "  float4 source_proj_l; float4 source_proj_r;\n"
        "};\n"
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
        "float4 ps_vr_pose_residual(VSOut i) : SV_Target {\n"
        "  float4 cur = current_frame.SampleLevel(linear_smp, i.uv, 0);\n"
        "  const float crop = 0.60;\n"
        "  const float start = 0.5 - crop * 0.5;\n"
        "  float rightEye = i.uv.x >= 0.5 ? 1.0 : 0.0;\n"
        "  float eyeId = rightEye;\n"
        "  float2 euv = float2(i.uv.x * 2.0 - eyeId, i.uv.y);\n"
        "  float2 curLocal = (euv - start.xx) / crop;\n"
        "  if (any(curLocal <= 0.0) || any(curLocal >= 1.0)) return float4(cur.rgb,1.0);\n"
        "  float4 cp = rightEye > 0.5 ? current_proj_r : current_proj_l;\n"
        "  float4 sp = rightEye > 0.5 ? source_proj_r : source_proj_l;\n"
        "  float tx = lerp(cp.x, cp.y, euv.x);\n"
        "  float ty = lerp(cp.w, cp.z, euv.y);\n"
        "  float3 dcur = float3(tx, ty, -1.0);\n"
        "  float3 dsrc = float3(dot(pose_r0.xyz,dcur), dot(pose_r1.xyz,dcur), dot(pose_r2.xyz,dcur));\n"
        "  if (dsrc.z >= -1e-4) return float4(cur.rgb,1.0);\n"
        "  float invz = -1.0 / dsrc.z;\n"
        "  float stx = dsrc.x * invz;\n"
        "  float sty = dsrc.y * invz;\n"
        "  float2 seuv = float2((stx-sp.x)/max(sp.y-sp.x,1e-6), (sty-sp.w)/min(sp.z-sp.w,-1e-6));\n"
        "  if (any(seuv < 0.0) || any(seuv > 1.0)) return float4(cur.rgb,1.0);\n"
        "  float2 srcLocal = (seuv - start.xx) / crop;\n"
        "  if (any(srcLocal <= 0.0) || any(srcLocal >= 1.0)) return float4(cur.rgb,1.0);\n"
        "  float srcU = eyeId * 0.5 + saturate(srcLocal.x) * 0.5;\n"
        "  float2 puv = float2(srcU, saturate(srcLocal.y));\n"
        "  float3 nr = nr_frame.SampleLevel(linear_smp, puv, 0).rgb;\n"
        "  float3 old = source_frame.SampleLevel(linear_smp, puv, 0).rgb;\n"
        "  float3 correction = clamp(nr - old, -0.30, 0.30);\n"
        "  float curEdge = max(abs(curLocal.x-0.5),abs(curLocal.y-0.5))*2.0;\n"
        "  float srcEdge = max(abs(srcLocal.x-0.5),abs(srcLocal.y-0.5))*2.0;\n"
        "  float aCur = 1.0 - smoothstep(0.58,0.995,curEdge);\n"
        "  float aSrc = 1.0 - smoothstep(0.58,0.995,srcEdge);\n"
        "  float a = min(aCur,aSrc);\n"
        "  return float4(saturate(cur.rgb + correction * a), 1.0);\n"
        "}\n";

    HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");
    auto compile = m != nullptr ? reinterpret_cast<pD3DCompile>(GetProcAddress(m, "D3DCompile")) : nullptr;
    if (compile == nullptr)
    {
        VrResidualRelease();
        return false;
    }

    ID3DBlob *blob = nullptr, *err = nullptr;
    HRESULT hr = compile(kResidualSrc, sizeof(kResidualSrc) - 1, "vrposeresidual", nullptr, nullptr,
                         "ps_vr_pose_residual", "ps_4_0", 0, 0, &blob, &err);
    if (FAILED(hr))
    {
        Log("[feed] VR pose residual PS compile failed 0x%08X: %s", hr,
            err ? (const char *)err->GetBufferPointer() : "");
        SafeRelease(err); SafeRelease(blob);
        VrResidualRelease();
        return false;
    }
    SafeRelease(err);

    hr = g.dev11->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_vr_residual.ps);
    SafeRelease(blob);
    if (FAILED(hr))
    {
        Log("[feed] VR pose residual PS creation failed 0x%08X", hr);
        VrResidualRelease();
        return false;
    }

    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth = sizeof(VrResidualConstants);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g.dev11->CreateBuffer(&cbd, nullptr, &g_vr_residual.pose_cb)))
    {
        Log("[feed] VR pose residual constant buffer creation failed");
        VrResidualRelease();
        return false;
    }

    Log("[feed] VR POSE-REPROJECTED RESIDUAL ready: accumulated OpenVR current->source rotation");
    return true;
}

static bool VrResidualCapturePending(ID3D11DeviceContext *ctx, ID3D11Texture2D *backbuffer,
                                     UINT source_w, UINT source_h)
{
    if (!VrResidualEnsure(backbuffer, source_w, source_h))
        return false;
    ctx->CopyResource(g_vr_residual.pending_src, g.tex11[SLOT_COLOR]);
    g_vr_residual.pending_valid = true;
    return true;
}

static bool VrResidualPromoteCompleted(ID3D11DeviceContext *ctx, ID3D11Texture2D *backbuffer,
                                       UINT source_w, UINT source_h)
{
    if (!VrResidualEnsure(backbuffer, source_w, source_h) || !g_vr_residual.pending_valid)
        return false;

    ctx->CopyResource(g_vr_residual.display_nr, g.tex11[SLOT_OUTPUT]);
    ctx->CopyResource(g_vr_residual.display_src, g_vr_residual.pending_src);
    g_vr_residual.display_valid = true;
    g_vr_residual.pending_valid = false;
    return true;
}

static bool VrResidualCompositeCurrent(ID3D11DeviceContext *ctx, ID3D11Texture2D *backbuffer,
                                       ID3D11RenderTargetView *rtv,
                                       UINT source_w, UINT source_h)
{
    UINT crop_w = 0, crop_h = 0, work_w = 0, work_h = 0;
    if (!g_vr_residual.display_valid ||
        !g_vr_pose_carry.display_valid ||
        !g_vr_pose_carry.current_valid ||
        !VrFoveatedLayout(source_w, source_h, &crop_w, &crop_h, &work_w, &work_h) ||
        !VrResidualEnsure(backbuffer, source_w, source_h))
        return false;

    VrResidualConstants c = {};
    c.pose_r0[0] = g_vr_pose_carry.display_current_to_source.m[0];
    c.pose_r0[1] = g_vr_pose_carry.display_current_to_source.m[1];
    c.pose_r0[2] = g_vr_pose_carry.display_current_to_source.m[2];
    c.pose_r1[0] = g_vr_pose_carry.display_current_to_source.m[3];
    c.pose_r1[1] = g_vr_pose_carry.display_current_to_source.m[4];
    c.pose_r1[2] = g_vr_pose_carry.display_current_to_source.m[5];
    c.pose_r2[0] = g_vr_pose_carry.display_current_to_source.m[6];
    c.pose_r2[1] = g_vr_pose_carry.display_current_to_source.m[7];
    c.pose_r2[2] = g_vr_pose_carry.display_current_to_source.m[8];
    VrCopy4(c.current_proj_l, g_vr_pose_carry.current_proj_l);
    VrCopy4(c.current_proj_r, g_vr_pose_carry.current_proj_r);
    VrCopy4(c.source_proj_l, g_vr_pose_carry.display_source_proj_l);
    VrCopy4(c.source_proj_r, g_vr_pose_carry.display_source_proj_r);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(ctx->Map(g_vr_residual.pose_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return false;
    memcpy(mapped.pData, &c, sizeof(c));
    ctx->Unmap(g_vr_residual.pose_cb, 0);

    ID3D11RenderTargetView   *old_rtv = nullptr;
    ID3D11DepthStencilView   *old_dsv = nullptr;
    ID3D11VertexShader       *old_vs  = nullptr;
    ID3D11PixelShader        *old_ps  = nullptr;
    ID3D11ShaderResourceView *old_srvs[3] = {};
    ID3D11SamplerState       *old_smp = nullptr;
    ID3D11Buffer             *old_cb  = nullptr;
    ID3D11InputLayout        *old_il  = nullptr;
    ID3D11BlendState         *old_bs  = nullptr; FLOAT old_bf[4] = {}; UINT old_mask = 0;
    ID3D11DepthStencilState  *old_ds  = nullptr; UINT old_sref = 0;
    ID3D11RasterizerState    *old_rs  = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY old_topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    UINT nvp = 1; D3D11_VIEWPORT old_vp = {};

    ctx->OMGetRenderTargets(1, &old_rtv, &old_dsv);
    ctx->VSGetShader(&old_vs, nullptr, nullptr);
    ctx->PSGetShader(&old_ps, nullptr, nullptr);
    ctx->PSGetShaderResources(0, 3, old_srvs);
    ctx->PSGetSamplers(0, 1, &old_smp);
    ctx->PSGetConstantBuffers(0, 1, &old_cb);
    ctx->IAGetInputLayout(&old_il);
    ctx->IAGetPrimitiveTopology(&old_topo);
    ctx->OMGetBlendState(&old_bs, old_bf, &old_mask);
    ctx->OMGetDepthStencilState(&old_ds, &old_sref);
    ctx->RSGetState(&old_rs);
    ctx->RSGetViewports(&nvp, &old_vp);

    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->CopyResource(g_vr_residual.current_tex, backbuffer);

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(source_w);
    vp.Height = static_cast<float>(source_h);
    vp.MaxDepth = 1.0f;

    ID3D11RenderTargetView *target[] = { rtv };
    ID3D11ShaderResourceView *srvs[3] = {
        g_vr_residual.current_srv,
        g_vr_residual.display_nr_srv,
        g_vr_residual.display_src_srv
    };
    ID3D11SamplerState *smp[] = { g.blit_sampler };

    ctx->OMSetRenderTargets(1, target, nullptr);
    ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->RSSetState(nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(g.blit_vs, nullptr, 0);
    ctx->PSSetShader(g_vr_residual.ps, nullptr, 0);
    ctx->PSSetSamplers(0, 1, smp);
    ctx->PSSetConstantBuffers(0, 1, &g_vr_residual.pose_cb);
    ctx->PSSetShaderResources(0, 3, srvs);
    ctx->Draw(3, 0);

    ID3D11ShaderResourceView *null_srvs[3] = {};
    ctx->PSSetShaderResources(0, 3, null_srvs);

    ctx->OMSetRenderTargets(1, &old_rtv, old_dsv);
    ctx->VSSetShader(old_vs, nullptr, 0);
    ctx->PSSetShader(old_ps, nullptr, 0);
    ctx->PSSetShaderResources(0, 3, old_srvs);
    ctx->PSSetSamplers(0, 1, &old_smp);
    ctx->PSSetConstantBuffers(0, 1, &old_cb);
    ctx->IASetInputLayout(old_il);
    ctx->IASetPrimitiveTopology(old_topo);
    ctx->OMSetBlendState(old_bs, old_bf, old_mask);
    ctx->OMSetDepthStencilState(old_ds, old_sref);
    ctx->RSSetState(old_rs);
    if (nvp) ctx->RSSetViewports(1, &old_vp);

    SafeRelease(old_rtv); SafeRelease(old_dsv); SafeRelease(old_vs); SafeRelease(old_ps);
    for (auto *&s : old_srvs) SafeRelease(s);
    SafeRelease(old_smp); SafeRelease(old_cb); SafeRelease(old_il); SafeRelease(old_bs);
    SafeRelease(old_ds); SafeRelease(old_rs);
    return true;
}

// v10: async fence is process/runtime-visible so SteamVR runtime teardown can
// safely retire it instead of destroying/reloading around live NR work.
static UINT64 g_vr_async_pending = 0;


"@

    $helperInsertAt = $src.IndexOf('static bool CopyOrResampleInputs(')
    if ($helperInsertAt -lt 0) { Fail 'CopyOrResampleInputs function not found for helper insertion.' }
    if ($src.IndexOf('static bool CopyOrResampleInputs(', $helperInsertAt + 1) -ge 0) {
        Fail 'CopyOrResampleInputs appeared more than once; refusing to guess.'
    }
    $src = $src.Substring(0, $helperInsertAt) + $helperCode + "`r`n" + $src.Substring($helperInsertAt)

    # Enter the packed-copy path before the ordinary whole-frame resample.
    $copyOpenPattern = '(?s)(static bool CopyOrResampleInputs\(ID3D11DeviceContext \*ctx,.*?UINT source_w, UINT source_h\)\s*\{)'
    $copyOpenMatches = [regex]::Matches($src, $copyOpenPattern)
    if ($copyOpenMatches.Count -ne 1) { Fail ('CopyOrResampleInputs anchor count=' + $copyOpenMatches.Count) }
    $m = $copyOpenMatches[0]
    $copyInject = $m.Groups[1].Value + @"

    UINT vr_crop_w = 0, vr_crop_h = 0, vr_work_w = 0, vr_work_h = 0;
    if (g.runtime != nullptr && g.runtime->get_hwnd() == 0 &&
        VrFoveatedLayout(source_w, source_h, &vr_crop_w, &vr_crop_h, &vr_work_w, &vr_work_h) &&
        g.width == vr_work_w && g.height == vr_work_h)
        return VrFoveatedPackInputs(ctx, color, mv, depth, mask, source_w, source_h);
"@
    $src = $src.Substring(0,$m.Index) + $copyInject + $src.Substring($m.Index+$m.Length)

    $cropUniformCallback = @'
static void VrPrepareGuideBounds(reshade::api::effect_runtime *rt, reshade::api::command_list *,
                                 reshade::api::resource_view rtv, reshade::api::resource_view)
{
    if (rt->get_hwnd() != 0) return;
    auto *device = rt->get_device();
    const auto resource = device->get_resource_from_view(rtv);
    if (resource.handle == 0) return;
    const auto desc = device->get_resource_desc(resource);
    UINT cw=0,ch=0,ww=0,wh=0;
    float bounds[4] = {0,0,1,1};
    const bool supported = device->get_api() == reshade::api::device_api::d3d11 ||
        (device->get_api() == reshade::api::device_api::d3d12 &&
         desc.texture.format != reshade::api::format::r10g10b10a2_unorm);
    if (supported && g_cfg.mode >= 2 && VrFoveatedLayout(desc.texture.width,desc.texture.height,&cw,&ch,&ww,&wh))
    {
        const UINT ew=desc.texture.width/2u;
        bounds[0]=float((ew-cw)/2u)/ew;bounds[1]=float((desc.texture.height-ch)/2u)/desc.texture.height;
        bounds[2]=float(cw)/ew;bounds[3]=float(ch)/desc.texture.height;
    }
    const auto uniform = rt->find_uniform_variable("DLSS5_Feed.fx","VR_FOVEA_BOUNDS");
    if (uniform.handle != 0) rt->set_uniform_value_float(uniform,bounds,4);
}

'@
    $src = Replace-LiteralOnce $src 'static bool CopyOrResampleInputs(' ($cropUniformCallback + 'static bool CopyOrResampleInputs(') 'prepare actual packed history bounds'
    $src = Replace-LiteralOnce $src '        reshade::register_event<reshade::addon_event::reshade_render_technique>(OnRenderTechnique);' ('        reshade::register_event<reshade::addon_event::reshade_begin_effects>(VrPrepareGuideBounds);' + "`n" + '        reshade::register_event<reshade::addon_event::reshade_render_technique>(OnRenderTechnique);') 'register VR crop bounds'
    $src = Replace-LiteralOnce $src '        reshade::unregister_event<reshade::addon_event::reshade_render_technique>(OnRenderTechnique);' ('        reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(VrPrepareGuideBounds);' + "`n" + '        reshade::unregister_event<reshade::addon_event::reshade_render_technique>(OnRenderTechnique);') 'unregister VR crop bounds'

    # Scope the remaining edits to FeedFrame11 only.
    $ffStart = $src.IndexOf('static void FeedFrame11(')
    if ($ffStart -lt 0) { Fail 'FeedFrame11 start not found' }
    $ffEnd = $src.IndexOf('// #62:', $ffStart)
    if ($ffEnd -lt 0) { Fail 'FeedFrame11 end anchor not found' }
    $before = $src.Substring(0,$ffStart)
    $ff = $src.Substring($ffStart,$ffEnd-$ffStart)
    $after = $src.Substring($ffEnd)

    $dimPattern = '(?ms)^[ \t]*const bool sr_wanted = g_cfg\.work_upscale == 2 && g_cfg\.mode >= 2 && g_cfg\.work_resolution < 100;\r?\n[ \t]*const UINT work_w = sr_wanted \? ScaledExtentUp\(cd\.Width,\s*g_cfg\.work_resolution\) : ScaledExtent\(cd\.Width,\s*g_cfg\.work_resolution\);\r?\n[ \t]*const UINT work_h = sr_wanted \? ScaledExtentUp\(cd\.Height,\s*g_cfg\.work_resolution\) : ScaledExtent\(cd\.Height,\s*g_cfg\.work_resolution\);\r?\n[ \t]*const bool want_sr = sr_wanted && \(work_w != cd\.Width \|\| work_h != cd\.Height\);'
    $dimReplace = @"
    UINT vr_crop_w = 0, vr_crop_h = 0, vr_work_w = 0, vr_work_h = 0;
    const bool vr_headset_runtime = g.runtime != nullptr && g.runtime->get_hwnd() == 0;
    const bool vr_foveated = vr_headset_runtime && VrFoveatedLayout(cd.Width, cd.Height, &vr_crop_w, &vr_crop_h, &vr_work_w, &vr_work_h);
    const bool sr_wanted = !vr_foveated && g_cfg.work_upscale == 2 && g_cfg.mode >= 2 && g_cfg.work_resolution < 100;
    const UINT work_w = vr_foveated ? vr_work_w :
        (sr_wanted ? ScaledExtentUp(cd.Width, g_cfg.work_resolution) : ScaledExtent(cd.Width, g_cfg.work_resolution));
    const UINT work_h = vr_foveated ? vr_work_h :
        (sr_wanted ? ScaledExtentUp(cd.Height, g_cfg.work_resolution) : ScaledExtent(cd.Height, g_cfg.work_resolution));
    const bool want_sr = sr_wanted && (work_w != cd.Width || work_h != cd.Height);

    if (vr_foveated)
    {
        static bool said_vr = false;
        if (!said_vr)
        {
            said_vr = true;
            const UINT eye_w = cd.Width / 2u;
            const UINT inset_x = (eye_w - vr_crop_w) / 2u;
            const UINT inset_y = (cd.Height - vr_crop_h) / 2u;
            UINT vr_wp = 60u, vr_hp = 50u; VrFoveationPercent(&vr_wp, &vr_hp);
            const UINT vr_budget_x10 = (vr_wp * vr_hp) / 10u;
            Log("[feed] VR FOVEATION LAYOUT SELECTED: source=%ux%u, each eye=%ux%u, center=%ux%u (%u%% x %u%%), packed work=%ux%u, NR pixels=%u.%u%%, margins x=%u y=%u",
                cd.Width, cd.Height, eye_w, cd.Height, vr_crop_w, vr_crop_h,
                vr_wp, vr_hp, vr_work_w, vr_work_h, vr_budget_x10 / 10u, vr_budget_x10 % 10u, inset_x, inset_y);
        }
    }
"@
    $ff = Replace-RegexOnce $ff $dimPattern $dimReplace 'replace FeedFrame11 work dimensions'
    $historyGuardReset = @'
const bool vr_history_reset = VrHistoryGuard(rt);
                const int reset = (g.need_reset || g_cfg.reset_every || vr_history_reset) ? 1 : 0;
'@
    $ff = Replace-LiteralOnce $ff 'const int reset = (g.need_reset || g_cfg.reset_every) ? 1 : 0;' $historyGuardReset 'VR temporal history guard'
    # Pose vectors are already exact source-pixel displacements, including SBS geometry.
    # Calibration belongs to estimated optical flow; do not scale geometric pose vectors again.
    $ff = Replace-LiteralOnce $ff 'ep.InMVScaleX        = g_cfg.mv_scale_x;' 'ep.InMVScaleX        = VrMotionScale(rt, g_cfg.mv_scale_x);' 'exact pose MV scale X'
    $ff = Replace-LiteralOnce $ff 'ep.InMVScaleY        = g_cfg.mv_scale_y;' 'ep.InMVScaleY        = VrMotionScale(rt, g_cfg.mv_scale_y);' 'exact pose MV scale Y'

    # Exact ownership exists only during the foveated D3D11 outer evaluate.
    $scopeEvaluate = @'
                g_vr_current_input = vr_foveated ? VrCurrentInputScope{g.list, ep.Feature.pInColor, ep.Feature.pInOutput} : VrCurrentInputScope{};
                NVSDK_NGX_Result re = SafeEvaluateDLSS(&ep, &ecode);
                g_vr_current_input = {};
'@
    $ff = Replace-LiteralOnce $ff '                NVSDK_NGX_Result re = SafeEvaluateDLSS(&ep, &ecode);' $scopeEvaluate 'VR native SR input ownership scope'

    $blitNeedle = 'BlitOutputToBackbuffer(ctx, rtv11);'
    $blitCount = ([regex]::Matches($ff, [regex]::Escape($blitNeedle))).Count
    if ($blitCount -ne 2) { Fail ('FeedFrame11 expected 2 output blits, found ' + $blitCount) }
    $blitReplace = @"
if (vr_foveated)
            {
                if (!VrFoveatedFeatherHome(ctx, rtv11, cd.Width, cd.Height))
                {
                    static bool said_home = false;
                    if (!said_home) { said_home = true; Log("[feed] VR box feather copy-home failed; native frame left untouched"); }
                }
            }
            else
                BlitOutputToBackbuffer(ctx, rtv11);
"@
    $ff = $ff.Replace($blitNeedle, $blitReplace)

    $src = $before + $ff + $after
    # Same-device D3D12 VR uses the same exact-resource ownership contract.
    # Without this scope, the preliminary DLAA bypass is never authorized there.
    $ff12Start = $src.IndexOf('static void FeedFrame12(')
    $ff12End = $src.IndexOf('static void FeedFrameVk(', $ff12Start)
    if ($ff12End -lt 0) { $ff12End = $src.IndexOf('static void FeedFrame', $ff12Start + 30) }
    if ($ff12Start -lt 0 -or $ff12End -lt 0) { Fail 'D3D12 current-input scope boundaries missing' }
    $ff12 = $src.Substring($ff12Start, $ff12End - $ff12Start)
    $scope12 = @'
                g_vr_current_input = rt->get_hwnd() == 0 ? VrCurrentInputScope{g.list, ep.Feature.pInColor, ep.Feature.pInOutput} : VrCurrentInputScope{};
                NVSDK_NGX_Result re = SafeEvaluateDLSS(&ep, &ecode);
                g_vr_current_input = {};
'@
    $ff12 = Replace-LiteralOnce $ff12 '                NVSDK_NGX_Result re = SafeEvaluateDLSS(&ep, &ecode);' $scope12 'D3D12 exact VR current-input evaluate scope'
    $ff12 = & (Join-Path $Root 'native\Patch-D3D12-VRFoveation.ps1') $ff12
    $src = $src.Substring(0, $ff12Start) + $ff12 + $src.Substring($ff12End)
    $vr12Helper = [IO.File]::ReadAllText((Join-Path $Root 'native\vr-foveated-d3d12.h'))
    $src = Replace-LiteralOnce $src 'static void FeedFrame12(' ($vr12Helper + "`nstatic void FeedFrame12(") 'D3D12 VR helper implementation'
    $src = Replace-LiteralOnce $src 'static void ReleaseFrameResources()' "static void Vr12Release();`nstatic void ReleaseFrameResources()" 'D3D12 VR teardown declaration'
    $src = Replace-RegexOnce $src '    DrainGpu\(\);\r?\n    FeedVkProbeRelease\(\);' "    DrainGpu();`n    Vr12Release();`n    FeedVkProbeRelease();" 'D3D12 VR fence before compositor retirement'

    # v5 fix 1: allow compatible TYPELESS/UNORM RGBA8 copy family.
    $oldPackFormat = @'
    if (src_cd.Format != dst_cd.Format || src_cd.Format != out_cd.Format)
    {
        static bool said = false;
        if (!said)
        {
            said = true;
            Log("[feed] VR foveated path: copy formats differ (backbuffer=%s color=%s output=%s); leaving frame native",
                FormatName(src_cd.Format), FormatName(dst_cd.Format), FormatName(out_cd.Format));
        }
        return false;
    }
'@
    $newPackFormat = @'
    const bool color_compatible =
        VrCopyFormatsCompatible(src_cd.Format, dst_cd.Format) &&
        VrCopyFormatsCompatible(src_cd.Format, out_cd.Format);
    if (!color_compatible)
    {
        static bool said = false;
        if (!said)
        {
            said = true;
            Log("[feed] VR foveated path: incompatible copy formats (backbuffer=%s color=%s output=%s); leaving frame native",
                FormatName(src_cd.Format), FormatName(dst_cd.Format), FormatName(out_cd.Format));
        }
        return false;
    }
'@
    $src = Replace-LiteralOnce $src $oldPackFormat $newPackFormat 'VR input format compatibility'

    $oldHomeFormat = '    if (dst_desc.Format != src_desc.Format) return false;'
    $newHomeFormat = @'
    if (!VrCopyFormatsCompatible(dst_desc.Format, src_desc.Format)) return false;
'@
    $src = Replace-LiteralOnce $src $oldHomeFormat $newHomeFormat 'VR output format compatibility'

    # v5 fix 2: only feed the full packed SBS VR target.
    $ort = $src.IndexOf('static void OnRenderTechnique(')
    if ($ort -lt 0) { Fail 'OnRenderTechnique not found.' }
    $ord = $src.IndexOf('static void OnDestroyDevice(', $ort)
    if ($ord -lt 0) { Fail 'OnRenderTechnique end not found.' }
    $rbefore = $src.Substring(0,$ort)
    $rbody = $src.Substring($ort,$ord-$ort)
    $rafter = $src.Substring($ord)

    $renderOpen = @'
{
    if (!g_cfg.enabled) return;
'@
    $renderOpenV5 = @'
{
    if (!g_cfg.enabled) return;

    {
        // Only the no-window headset effect runtime is eligible. This keeps desktop
        // mirrors out without assuming one headset's resolution.
        if (rt->get_hwnd() != 0) return;
        reshade::api::device *vr_dev = rt->get_device();
        const reshade::api::resource vr_res =
            vr_dev != nullptr ? vr_dev->get_resource_from_view(rtv) : reshade::api::resource{};
        if (vr_dev == nullptr || vr_res.handle == 0) return;
        const reshade::api::resource_desc vr_desc = vr_dev->get_resource_desc(vr_res);
        const UINT vr_w = vr_desc.texture.width;
        const UINT vr_h = vr_desc.texture.height;
        if (!VrPackedSbsCandidate(vr_w, vr_h)) return;
    }
'@
    $rbody = Replace-LiteralOnce $rbody $renderOpen $renderOpenV5 'full-SBS VR target gate'
    $src = $rbefore + $rbody + $rafter

    # v5 fix 3: preserve the already-built foveated feature across ReShade's
    # transient VR runtime recreation instead of forcing DFC to rebuild.
    $initStart = $src.IndexOf('static void OnInitEffectRuntime(')
    if ($initStart -lt 0) { Fail 'OnInitEffectRuntime not found.' }
    $initEnd = $src.IndexOf('static void OnDestroyEffectRuntime(', $initStart)
    if ($initEnd -lt 0) { Fail 'OnInitEffectRuntime end not found.' }
    $ibefore = $src.Substring(0,$initStart)
    $ibody = $src.Substring($initStart,$initEnd-$initStart)
    $iafter = $src.Substring($initEnd)

    $invalidateLine = '        if (g.session_ready && g.dev12_owned) g.frame_ready = false;'
    $keepLine = @'
        if (g.session_ready && g.dev12_owned)
            Log("[feed] VR foveated runtime re-init: keeping existing NR feature/resources alive");
'@
    $ibody = Replace-LiteralOnce $ibody $invalidateLine $keepLine 'preserve feature across runtime re-init'
    $src = $ibefore + $ibody + $iafter

    # ------------------------------------------------------------------
    # v25.2 current-frame path:
    # DO NOT install the v8-v24 async/carry presentation rewrite.
    # FeedFrame11 remains on the original same-frame wait/handoff path:
    # current crop -> DFC/NR -> feather into the same current native frame.
    # ------------------------------------------------------------------

    # ------------------------------------------------------------------
    # v25.2 retains the proven OpenVR runtime ownership protections while
    # presentation itself stays strictly current-frame (no async NR carry).
    # ------------------------------------------------------------------

    $runtimeGlobalsAnchor = 'static ULONGLONG   g_bound_last_render;   // GetTickCount64 of the bound runtime''s last DLSS5_Feed render'
    $runtimeGlobalsReplace = @'
static ULONGLONG   g_bound_last_render;   // GetTickCount64 of the bound runtime's last DLSS5_Feed render
static reshade::api::effect_runtime *g_vr_runtime_owner = nullptr;
static ULONGLONG g_vr_runtime_hold_until = 0;
'@
    $src = Replace-LiteralOnce $src $runtimeGlobalsAnchor $runtimeGlobalsReplace 'restore sticky OpenVR runtime globals'

    $initFnAnchor = 'static void OnInitEffectRuntime(reshade::api::effect_runtime *rt)'
    $pauseHelper = @'
static void VrPauseAsyncForRuntimeChurn()
{
    // v25.2 has no async visible NR job to drain. FeedFrame11 waits for the
    // current result before returning. Only invalidate temporal history.
    g_vr_async_pending = 0;
    g.need_reset = true;
}

static void OnInitEffectRuntime(reshade::api::effect_runtime *rt)
'@
    $src = Replace-LiteralOnce $src $initFnAnchor $pauseHelper 'add safe VR async quiesce helper'

    $initBindAnchor = @'
    const bool rebind = g.runtime == nullptr || rt == g.runtime ||
                        (g.technique.handle == 0 && slot->technique.handle != 0);
'@
    $initBindReplace = @'
    const bool is_vr_runtime = rt->get_hwnd() == 0 && !slot->proxy;
    if (is_vr_runtime)
    {
        const bool newly_acquired = g_vr_runtime_owner != rt;
        g_vr_runtime_owner = rt;
        g_vr_runtime_hold_until = 0;
        if (newly_acquired)
            Log("[feed] VR runtime acquired: %p (device %p, OpenVR, window class '%s') -- headset runtime gets sticky priority over desktop/mirror runtimes",
                (void *)rt, slot->dev, slot->wclass);
    }

    const ULONGLONG now_owner = GetTickCount64();
    const bool vr_hold_active =
        g_vr_runtime_owner != nullptr ||
        (g_vr_runtime_hold_until != 0 && now_owner < g_vr_runtime_hold_until);

    const bool rebind = is_vr_runtime ||
                        (!vr_hold_active &&
                         (g.runtime == nullptr || rt == g.runtime ||
                          (g.technique.handle == 0 && slot->technique.handle != 0)));
'@
    $src = Replace-LiteralOnce $src $initBindAnchor $initBindReplace 'restore OpenVR sticky binding'

    # Remove the official-source feature re-create on VR runtime re-init.
    # The old working VR branch kept the live shared feature/resources through
    # this exact SteamVR runtime churn.
    $ois = $src.IndexOf('static void OnInitEffectRuntime(')
    $oie = $src.IndexOf('static void OnDestroyEffectRuntime(', $ois)
    if ($ois -lt 0 -or $oie -lt 0) { Fail 'runtime patch: OnInit bounds missing' }
    $pre = $src.Substring(0,$ois)
    $body = $src.Substring($ois,$oie-$ois)
    $post = $src.Substring($oie)

    $recreateLine = '        if (g.session_ready && g.dev12_owned) g.frame_ready = false;'
    if ($body.Contains($recreateLine))
    {
        $body = $body.Replace($recreateLine,
'        if (g.session_ready && g.dev12_owned)
            Log("[feed] VR runtime re-init: keeping existing NR feature/resources alive");')
    }

    # Only arm create grace when there is genuinely no ready feature.
    $graceLine = '    g.create_grace = 0;'
    $graceReplacement = @'
    if (!g.frame_ready)
        g.create_grace = 0;
'@
    $body = Replace-LiteralOnce $body $graceLine $graceReplacement 'suppress needless create grace on runtime churn'
    $src = $pre + $body + $post

    $destroyPrologue = @'
    const bool was_bound = rt == g.runtime;
    UntrackRuntime(rt);
'@
    $destroyReplace = @'
    const bool was_bound = rt == g.runtime;
    const bool was_vr_owner = rt == g_vr_runtime_owner;
    if (was_vr_owner)
    {
        VrPauseAsyncForRuntimeChurn();
        g_vr_runtime_owner = nullptr;
        g_vr_runtime_hold_until = GetTickCount64() + 10000;
        Log("[feed] VR runtime %p destroyed -- holding desktop/mirror takeover for up to 10 s while the OpenVR runtime is recreated",
            (void *)rt);
    }
    UntrackRuntime(rt);
'@
    $src = Replace-LiteralOnce $src $destroyPrologue $destroyReplace 'protect current-frame VR runtime destroy'

    $reloadAnchor = @'
    RuntimeSlot *slot = TrackRuntime(rt);
    if (rt == g.runtime || g.runtime == nullptr || (g.technique.handle == 0 && slot->technique.handle != 0))
'@
    $reloadReplace = @'
    RuntimeSlot *slot = TrackRuntime(rt);
    const ULONGLONG now_reload = GetTickCount64();
    const bool vr_hold_reload =
        g_vr_runtime_owner != nullptr ||
        (g_vr_runtime_hold_until != 0 && now_reload < g_vr_runtime_hold_until);
    const bool is_vr_reload = rt->get_hwnd() == 0 && !slot->proxy;

    if (!is_vr_reload && vr_hold_reload)
    {
        static int ignored_desktop_reload = 0;
        if (++ignored_desktop_reload <= 6)
            Log("[feed] ignoring desktop/mirror effect reload on runtime %p while OpenVR VR ownership is active",
                (void *)rt);
        return;
    }

    if (rt == g.runtime || g.runtime == nullptr || (g.technique.handle == 0 && slot->technique.handle != 0))
'@
    $src = Replace-LiteralOnce $src $reloadAnchor $reloadReplace 'ignore desktop reloads during VR ownership'

    # v26: runtime VR foveation controls in ReShade > Add-ons > DLSS 5 Feed.
    $vrOverlayAnchor = @'
    ImGui::Separator();
    ImGui::TextUnformatted("DLSS contract");
'@
    $vrOverlayReplace = @'
    ImGui::Separator();
    ImGui::TextUnformatted("VR foveation");
    if (g.runtime != nullptr && g.runtime->get_hwnd() == 0 && g_vr_foveation_enabled)
    {
        if (g_vr_neural_weight <= 0.0f)
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "Headset output: native fallback (neural result hidden)");
        else if (g_vr_neural_weight < 1.0f)
            ImGui::Text("Headset output: neural recovery %.0f%%", g_vr_neural_weight * 100.0f);
        else
            ImGui::TextUnformatted("Headset output: neural result visible");
    }
    VrFoveationCfgLoad();
    bool vr_fov_enabled = g_vr_foveation_enabled != 0;
    if (ImGui::Checkbox("Enable VR foveation", &vr_fov_enabled))
    {
        g_vr_foveation_enabled = vr_fov_enabled ? 1 : 0;
        VrFoveationCfgSave();
        g.frame_ready = false;
        Log("[feed] VR foveation %s from overlay", vr_fov_enabled ? "enabled" : "disabled");
    }
    static const char *kVrFoveaPresets[] = { "Custom", "Small (50% x 45%)", "Balanced (60% x 50%)", "Wide (70% x 50%)", "Large (75% x 60%)" };
    int vr_fov_preset = g_vr_foveation_preset;
    if (ImGui::Combo("VR foveation size", &vr_fov_preset, kVrFoveaPresets, 5))
    {
        g_vr_foveation_preset = vr_fov_preset;
        VrFoveationCfgSave();
        g.frame_ready = false;
    }
    if (g_vr_foveation_preset == 0)
    {
        const int old_w = g_vr_foveation_width, old_h = g_vr_foveation_height;
        ImGui::SliderInt("VR fovea width (%)", &g_vr_foveation_width, 35, 90);
        ImGui::SliderInt("VR fovea height (%)", &g_vr_foveation_height, 35, 90);
        if (old_w != g_vr_foveation_width || old_h != g_vr_foveation_height)
        {
            VrFoveationClamp(); VrFoveationCfgSave(); g.frame_ready = false;
        }
    }
    UINT vr_ui_w = 60u, vr_ui_h = 50u; VrFoveationPercent(&vr_ui_w, &vr_ui_h);
    ImGui::TextDisabled("Active center: %u%% x %u%% per eye (%.1f%% of pixels). Broad packed-SBS auto sizing.",
                        vr_ui_w, vr_ui_h, (vr_ui_w * vr_ui_h) / 100.0f);
    ImGui::SameLine(); HelpMarker("Foveation affects only the headset no-window packed side-by-side VR buffer. Outer pixels stay current native. Balanced matches the working v25 60x50 region.");

    ImGui::Separator();
    ImGui::TextUnformatted("DLSS contract");
'@
    $src = Replace-LiteralOnce $src $vrOverlayAnchor $vrOverlayReplace 'VR foveation overlay controls'

    # Safety checks before compile.
    if ($src -notmatch 'vr-universal-foveated26') { Fail 'post-patch check: version marker missing' }
    if ($src -notmatch 'VR FOVEATION COPY-HOME ACTIVE') { Fail 'post-patch check: current-frame VR presentation marker missing' }
    if ($src -notmatch 'VrPackedSbsCandidate') { Fail 'post-patch check: broad packed-SBS candidate helper missing' }
    if ($src -notmatch 'source_w > 16384u') { Fail 'post-patch check: broad D3D11 VR bounds missing' }
    if ($src -notmatch 'rt->get_hwnd\(\) != 0') { Fail 'post-patch check: headless VR runtime gate missing' }
    if ($src -notmatch 'vr_foveation_preset') { Fail 'post-patch check: configurable foveation preset missing' }
    if ($src -notmatch 'Enable VR foveation') { Fail 'post-patch check: ReShade foveation checkbox missing' }
    if ($src -notmatch 'VR foveation size') { Fail 'post-patch check: ReShade foveation preset dropdown missing' }
    if ($src -notmatch 'VrFoveatedPackInputs') { Fail 'post-patch check: current-frame crop packing missing' }
    if ($src -notmatch 'VrFoveatedFeatherHome') { Fail 'post-patch check: current-frame feather composite missing' }
    if ($src -notmatch 'VR CURRENT-FRAME FEATHER ready') { Fail 'post-patch check: feather marker missing' }
    if ($src -notmatch 'VrCopyFormatsCompatible') { Fail 'post-patch check: DXGI copy-family compatibility patch missing' }
    if ($src -notmatch 'headset runtime gets sticky priority') { Fail 'post-patch check: sticky OpenVR ownership missing' }
    if ($src -notmatch 'ignoring desktop/mirror effect reload') { Fail 'post-patch check: desktop reload guard missing' }
    if ($src -notmatch 'keeping existing NR feature/resources alive') { Fail 'post-patch check: feature persistence missing' }
    if ($src -match 'kVrFoveatedAxisPercent') { Fail 'post-patch check: stale square-crop symbol remains' }
    if ($src -notmatch 'crop_start\[4\]') { Fail 'post-patch check: rectangular temporal-MV geometry missing' }

    # Absolutely no visible async/carry path in this build.
    if ($src -match 'VR TEMPORAL-MV submit') { Fail 'post-patch check: async temporal submit unexpectedly present' }
    if ($src -match 'VrResidualCompositeCurrent\(ctx, color, rtv11') { Fail 'post-patch check: stale residual presentation unexpectedly active' }

    Set-Content -LiteralPath $cppPath -Value $src -Encoding UTF8
    Say 'Source patch applied safely.'

    # ---- Build official addon
    Say 'Compiling dlss5-feed.addon64 with MSVC...'
    $buildLog = Join-Path $Work 'MSVC_BUILD_LOG.txt'
    $buildBat = Join-Path $srcRoot 'build.bat'
    if (-not (Test-Path -LiteralPath $buildBat -PathType Leaf)) {
        Fail ('Official build script is missing: ' + $buildBat)
    }

    # Do not depend on PowerShell/cmd inheriting the current directory. Generate a
    # one-use wrapper with absolute paths for the source tree, build script and log.
    $buildWrapper = Join-Path $Work 'RUN_FEEDER_BUILD.cmd'
    $wrapperText = @"
@echo off
cd /d "$srcRoot"
if errorlevel 1 exit /b 90
call "$buildBat" > "$buildLog" 2>&1
exit /b %errorlevel%
"@
    Set-Content -LiteralPath $buildWrapper -Value $wrapperText -Encoding ASCII

    $buildInfo = New-Object System.Diagnostics.ProcessStartInfo
    $buildInfo.FileName = $env:ComSpec
    $buildInfo.Arguments = '/d /s /c ""' + $buildWrapper + '""'
    $buildInfo.WorkingDirectory = $Work
    $buildInfo.UseShellExecute = $false
    $buildInfo.CreateNoWindow = $true
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $buildInfo
    if (-not $proc.Start()) { Fail 'Could not start MSVC build.' }

    $expectedAddon = Join-Path $srcRoot 'build\dlss5-feed.addon64'
    if (-not $proc.WaitForExit(300000)) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        Fail 'MSVC build timed out; no binary will be installed.'
    }
    $proc.Refresh()
    if (Test-Path -LiteralPath $buildLog -PathType Leaf) {
        Copy-Item -LiteralPath $buildLog -Destination (Join-Path $Out 'MSVC_BUILD_LOG.txt') -Force
    }
    if ($proc.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $expectedAddon -PathType Leaf)) {
        $tail = if (Test-Path -LiteralPath $buildLog) { (Get-Content -LiteralPath $buildLog -Tail 60) -join "`n" } else { 'No compiler log.' }
        Fail ("MSVC build failed (exit " + $proc.ExitCode + "). No binary installed.`n" + $tail)
    }

    $addon = Join-Path $srcRoot 'build\dlss5-feed.addon64'
    if (-not (Test-Path -LiteralPath $addon -PathType Leaf)) { Fail 'Build succeeded but addon64 was not produced.' }
    Copy-Item -LiteralPath $addon -Destination (Join-Path $Out 'dlss5-feed.addon64') -Force

    # Copy the known-good pose-MV components shipped beside this script.
    $shader = Join-Path $Root 'DLSS5_Feed.fx'
    $bridge = Join-Path $Root 'vr-pose-bridge-v1.addon64'
    if (-not (Test-Path -LiteralPath $shader)) { Fail 'Bundled known-good DLSS5_Feed.fx is missing.' }
    if (-not (Test-Path -LiteralPath $bridge)) { Fail 'Bundled known-good vr-pose-bridge-v1.addon64 is missing.' }
    Copy-Item -LiteralPath $shader -Destination (Join-Path $Out 'DLSS5_Feed.fx') -Force
    Copy-Item -LiteralPath $bridge -Destination (Join-Path $Out 'vr-pose-bridge-v1.addon64') -Force
    $depthBridge = Join-Path $Root 'vr-depth-bridge.addon64'
    if (-not (Test-Path -LiteralPath $depthBridge)) { Fail 'Bundled stereo depth bridge is missing.' }
    Copy-Item -LiteralPath $depthBridge -Destination (Join-Path $Out 'vr-depth-bridge.addon64') -Force
    $projectionBridge = Join-Path $Root 'vr-projection-bridge.addon64'
    if (-not (Test-Path -LiteralPath $projectionBridge)) { Fail 'Bundled stereo projection bridge is missing.' }
    Copy-Item -LiteralPath $projectionBridge -Destination (Join-Path $Out 'vr-projection-bridge.addon64') -Force
    $currentInput = Join-Path $Root 'vr-current-input.addon64'
    if (-not (Test-Path -LiteralPath $currentInput)) { Fail 'Bundled VR current input bridge is missing.' }
    Copy-Item -LiteralPath $currentInput -Destination (Join-Path $Out 'vr-current-input.addon64') -Force
    @(
      'VR Universal Current-Frame Foveation v26',
      'marker=vr-universal-foveated26.28',
      'default=balanced-60x50',
      'buffers=packed-sbs-headless-768..16384x384..16384'
    ) | Set-Content -LiteralPath (Join-Path $Out 'VR_FOVEATED_V26.txt') -Encoding UTF8
    @(
      'vr_foveation=1',
      'vr_foveation_preset=2',
      'vr_foveation_width=60',
      'vr_foveation_height=50'
    ) | Set-Content -LiteralPath (Join-Path $Out 'dlss5-feed-vr-defaults.cfg') -Encoding ASCII

    if ($BuildOnly) {
        Say ''
        Say ('VR UNIVERSAL CURRENT-FRAME FOVEATED v26 BUILD COMPLETE: ' + $Out)
        exit 0
    }

    # ---- Backup + install
    Say 'Backing up current game files...'
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $backup = Join-Path $GameDir ('VRFoveatedBackup-' + $stamp)
    New-Item -ItemType Directory -Path $backup -Force | Out-Null

    $gameAddon = Join-Path $GameDir 'dlss5-feed.addon64'
    $gameBridge = Join-Path $GameDir 'vr-pose-bridge-v1.addon64'
    $gameShader = Join-Path $GameDir 'reshade-shaders\Shaders\DLSS5_Feed.fx'
    foreach ($p in @($gameAddon,$gameBridge,$gameShader)) {
        if (Test-Path -LiteralPath $p -PathType Leaf) {
            Copy-Item -LiteralPath $p -Destination (Join-Path $backup ([IO.Path]::GetFileName($p))) -Force
        }
    }

    Say 'Installing universal current-frame foveated feeder with VR runtime protection...'
    Copy-Item -LiteralPath (Join-Path $Out 'dlss5-feed.addon64') -Destination $gameAddon -Force
    New-Item -ItemType Directory -Path (Split-Path -Parent $gameShader) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $Out 'DLSS5_Feed.fx') -Destination $gameShader -Force
    Copy-Item -LiteralPath (Join-Path $Out 'vr-pose-bridge-v1.addon64') -Destination $gameBridge -Force
    Copy-Item -LiteralPath (Join-Path $Out 'vr-depth-bridge.addon64') -Destination (Join-Path $GameDir 'vr-depth-bridge.addon64') -Force

    Say ''
    Copy-Item -LiteralPath (Join-Path $Out 'vr-projection-bridge.addon64') -Destination (Join-Path $GameDir 'vr-projection-bridge.addon64') -Force
    Copy-Item -LiteralPath (Join-Path $Out 'vr-current-input.addon64') -Destination (Join-Path $GameDir 'vr-current-input.addon64') -Force

    Say 'VR UNIVERSAL CURRENT-FRAME FOVEATED v26 BUILD + INSTALL COMPLETE'
    Say ('Backup: ' + $backup)
    Say ('Addon:  ' + $gameAddon)
    Say ''
    Say 'TEST: launch the VR game normally with your existing DFC settings.'
    Say 'Look for: [feed] VR FOVEATION COPY-HOME ACTIVE in dlss5-feed.log.'
    Say 'Report FPS and whether the center NR strips line up correctly in both eyes.'
}
catch {
    Fail $_.Exception.Message
}
