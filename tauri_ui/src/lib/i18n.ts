/**
 * i18n — Phase 7
 *
 * Simple reactive internationalisation for DisplaySwitch.
 * Supports: "en" | "zh" | "auto" (system detection).
 */

export type Locale = "en" | "zh";
export type LocaleMode = Locale | "auto";

// ── Translations ────────────────────────────────────────────────────────

const translations: Record<Locale, Record<string, string>> = {
  en: {
    // Header
    "app.title": "🖥 DisplaySwitch",
    "btn.refresh": "⟳ Refresh",
    "btn.scanning": "Scanning…",
    "btn.settings": "Settings",

    // MonitorCard
    "card.interface": "Interface",
    "card.resolution": "Resolution",
    "card.bandwidth": "Bandwidth",
    "card.panel": "Panel",
    "card.hdr": "HDR",
    "card.brightness": "Brightness",
    "card.brightness.na": "Not available (DDC/CI not supported)",
    "card.input": "Input",

    // Settings Panel
    "settings.title": "Settings",
    "settings.tab.general": "General",
    "settings.tab.hotkeys": "Hotkeys",
    "settings.tab.presets": "Presets",
    "settings.autostart": "Start on login",
    "settings.sync_interval": "Sync interval",
    "settings.theme": "Theme",
    "settings.theme.dark": "Dark",
    "settings.theme.light": "Light",
    "settings.theme.system": "System",
    "settings.language": "Language",
    "settings.lang.auto": "Auto",
    "settings.lang.en": "English",
    "settings.lang.zh": "中文",
    "settings.save": "Save",
    "settings.cancel": "Cancel",

    // Hotkeys
    "hotkey.add": "+ Add Hotkey",
    "hotkey.recording": "Press keys…",
    "hotkey.action.brightness_up": "Brightness Up",
    "hotkey.action.brightness_down": "Brightness Down",
    "hotkey.action.switch_input": "Switch Input",
    "hotkey.action.apply_preset": "Apply Preset",
    "hotkey.action.refresh": "Refresh",

    // Presets
    "preset.add": "+ Add Preset",
    "preset.apply": "Apply",
    "preset.empty": "No presets configured.",

    // Profiles
    "profile.title": "Profiles",
    "profile.work": "Work",
    "profile.gaming": "Gaming",
    "profile.presentation": "Presentation",
    "profile.movie": "Movie",
    "profile.custom": "Custom",
    "profile.active": "Active",
    "profile.activate": "Activate",
    "profile.add": "+ New Profile",

    // Toast
    "toast.brightness_changed": "Brightness: {0}%",
    "toast.input_switched": "Input switched to {0}",
    "toast.preset_applied": "Preset applied: {0}",
    "toast.profile_activated": "Profile: {0}",
    "toast.hotkey_registered": "Hotkeys registered",
    "toast.config_saved": "Settings saved",
    "toast.scan_complete": "Found {0} display(s)",
    "toast.error": "Error: {0}",

    // Status
    "status.no_displays": "No displays detected.",
    "status.not_tauri": "Not running inside Tauri window.",

    // StatusBar
    "statusbar.displays": "Connected displays",
    "statusbar.profile": "Active profile",
    "statusbar.last_sync": "Last sync",

    // Ambient Light Sensor
    "als.unavailable": "Sensor unavailable",
    "als.very_dark": "Very dark",
    "als.dim": "Dim",
    "als.indoor": "Indoor",
    "als.bright_indoor": "Bright indoor",
    "als.cloudy": "Cloudy",
    "als.daylight": "Daylight",
    "als.sunlight": "Direct sunlight",
    "als.auto_brightness": "Auto Brightness",
    "als.enabled": "Enabled",
    "als.disabled": "Disabled",
    "als.interval": "ALS polling interval",

    // Topology
    "topology.title": "Display Topology",
    "topology.show": "Show Topology",
    "topology.hide": "Hide Topology",

    // Updater
    "updater.check": "Check for Updates",
    "updater.checking": "Checking…",
    "updater.available": "Update available: v{0}",
    "updater.up_to_date": "You're up to date",
  },
  zh: {
    // Header
    "app.title": "🖥 DisplaySwitch",
    "btn.refresh": "⟳ 刷新",
    "btn.scanning": "扫描中…",
    "btn.settings": "设置",

    // MonitorCard
    "card.interface": "接口",
    "card.resolution": "分辨率",
    "card.bandwidth": "带宽",
    "card.panel": "面板",
    "card.hdr": "HDR",
    "card.brightness": "亮度",
    "card.brightness.na": "不可用 (DDC/CI 不支持)",
    "card.input": "输入源",

    // Settings Panel
    "settings.title": "设置",
    "settings.tab.general": "通用",
    "settings.tab.hotkeys": "快捷键",
    "settings.tab.presets": "预设",
    "settings.autostart": "开机自启",
    "settings.sync_interval": "同步间隔",
    "settings.theme": "主题",
    "settings.theme.dark": "深色",
    "settings.theme.light": "浅色",
    "settings.theme.system": "跟随系统",
    "settings.language": "语言",
    "settings.lang.auto": "自动",
    "settings.lang.en": "English",
    "settings.lang.zh": "中文",
    "settings.save": "保存",
    "settings.cancel": "取消",

    // Hotkeys
    "hotkey.add": "+ 添加快捷键",
    "hotkey.recording": "请按下按键…",
    "hotkey.action.brightness_up": "亮度增加",
    "hotkey.action.brightness_down": "亮度降低",
    "hotkey.action.switch_input": "切换输入",
    "hotkey.action.apply_preset": "应用预设",
    "hotkey.action.refresh": "刷新",

    // Presets
    "preset.add": "+ 添加预设",
    "preset.apply": "应用",
    "preset.empty": "暂无预设。",

    // Profiles
    "profile.title": "场景",
    "profile.work": "工作",
    "profile.gaming": "游戏",
    "profile.presentation": "演示",
    "profile.movie": "影音",
    "profile.custom": "自定义",
    "profile.active": "使用中",
    "profile.activate": "启用",
    "profile.add": "+ 新建场景",

    // Toast
    "toast.brightness_changed": "亮度: {0}%",
    "toast.input_switched": "输入已切换: {0}",
    "toast.preset_applied": "预设已应用: {0}",
    "toast.profile_activated": "场景: {0}",
    "toast.hotkey_registered": "快捷键已注册",
    "toast.config_saved": "设置已保存",
    "toast.scan_complete": "发现 {0} 个显示器",
    "toast.error": "错误: {0}",

    // Status
    "status.no_displays": "未检测到显示器。",
    "status.not_tauri": "未在 Tauri 窗口中运行。",

    // StatusBar
    "statusbar.displays": "已连接显示器",
    "statusbar.profile": "当前场景",
    "statusbar.last_sync": "最后同步",

    // Ambient Light Sensor
    "als.unavailable": "传感器不可用",
    "als.very_dark": "非常暗",
    "als.dim": "昏暗",
    "als.indoor": "室内光线",
    "als.bright_indoor": "明亮室内",
    "als.cloudy": "阴天户外",
    "als.daylight": "日光",
    "als.sunlight": "直射阳光",
    "als.auto_brightness": "自动亮度",
    "als.enabled": "已启用",
    "als.disabled": "已禁用",
    "als.interval": "光传感器轮询间隔",

    // Topology
    "topology.title": "显示器拓扑",
    "topology.show": "显示拓扑",
    "topology.hide": "隐藏拓扑",

    // Updater
    "updater.check": "检查更新",
    "updater.checking": "检查中…",
    "updater.available": "有新版本: v{0}",
    "updater.up_to_date": "已是最新版本",
  },
};

// ── State ───────────────────────────────────────────────────────────────

let _currentLocale: Locale = detectSystemLocale();
let _listeners: Array<() => void> = [];

function detectSystemLocale(): Locale {
  if (typeof navigator === "undefined") return "en";
  const lang = navigator.language?.toLowerCase() ?? "en";
  if (lang.startsWith("zh")) return "zh";
  return "en";
}

export function resolveLocale(mode: LocaleMode): Locale {
  return mode === "auto" ? detectSystemLocale() : mode;
}

export function setLocale(locale: Locale): void {
  _currentLocale = locale;
  for (const fn of _listeners) fn();
}

export function getLocale(): Locale {
  return _currentLocale;
}

export function onLocaleChange(fn: () => void): () => void {
  _listeners.push(fn);
  return () => {
    _listeners = _listeners.filter((f) => f !== fn);
  };
}

/**
 * Translate a key. Supports positional placeholders: {0}, {1}, etc.
 * Example: t("toast.brightness_changed", "75") => "Brightness: 75%"
 */
export function t(key: string, ...args: (string | number)[]): string {
  const table = translations[_currentLocale] ?? translations.en;
  let text = table[key] ?? translations.en[key] ?? key;
  for (let i = 0; i < args.length; i++) {
    text = text.replace(`{${i}}`, String(args[i]));
  }
  return text;
}
