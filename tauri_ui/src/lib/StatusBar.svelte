<!--
  StatusBar — Phase 7
  Bottom status bar showing: display count, active profile, ALS lux, sync status, version.
  Supports dark/light theme.
-->
<script lang="ts">
  import type { DisplayInfo, Profile } from "./types";
  import { t } from "./i18n";

  let {
    displays,
    activeProfile,
    ambientLux,
    ambientAvailable,
    lastSyncTime,
    loading,
  }: {
    displays: DisplayInfo[];
    activeProfile: Profile | null;
    ambientLux: number;
    ambientAvailable: boolean;
    lastSyncTime: Date | null;
    loading: boolean;
  } = $props();

  function luxDescription(lux: number): string {
    if (lux < 0) return t("als.unavailable");
    if (lux < 10) return t("als.very_dark");
    if (lux < 50) return t("als.dim");
    if (lux < 200) return t("als.indoor");
    if (lux < 500) return t("als.bright_indoor");
    if (lux < 1000) return t("als.cloudy");
    if (lux < 10000) return t("als.daylight");
    return t("als.sunlight");
  }

  function luxIcon(lux: number): string {
    if (lux < 10) return "🌙";
    if (lux < 200) return "💡";
    if (lux < 1000) return "⛅";
    return "☀️";
  }

  function formatTime(d: Date | null): string {
    if (!d) return "—";
    return d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
  }

  let internalCount = $derived(displays.filter((d) => d.is_internal).length);
  let externalCount = $derived(displays.filter((d) => !d.is_internal).length);
</script>

<footer class="status-bar">
  <!-- Display count -->
  <div class="status-item" title={t("statusbar.displays")}>
    <span class="status-text">
      {displays.length} {displays.length === 1 ? "display" : "displays"}
      {#if displays.length > 0}
        <span class="status-detail">({internalCount}int + {externalCount}ext)</span>
      {/if}
    </span>
  </div>

  <!-- Active profile -->
  {#if activeProfile}
    <div class="status-item" title={t("statusbar.profile")}>
      <span class="status-text">{activeProfile.icon ?? "▪"} {activeProfile.name}</span>
    </div>
  {/if}

  <!-- Ambient light sensor -->
  {#if ambientAvailable}
    <div class="status-item" title={`${luxDescription(ambientLux)} (${Math.round(ambientLux)} lux)`}>
      <span class="status-text">{Math.round(ambientLux)} lux</span>
    </div>
  {/if}

  <div class="spacer"></div>

  <!-- Sync status -->
  <div class="status-item" title={t("statusbar.last_sync")}>
    <span class="sync-dot" class:syncing={loading}>●</span>
    <span class="status-text">{formatTime(lastSyncTime)}</span>
  </div>

  <!-- Version -->
  <div class="status-item version">
    <span class="status-text">v2.19.0</span>
  </div>
</footer>

<style>
  .status-bar {
    position: fixed;
    bottom: 0;
    left: 52px; /* clear activity bar */
    right: 0;
    height: 24px;
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 0 12px;
    background: rgba(255, 255, 255, 0.02);
    border-top: 1px solid rgba(255, 255, 255, 0.06);
    font-size: 11px;
    color: rgba(169, 177, 214, 0.4);
    z-index: 500;
    user-select: none;
    font-family: -apple-system, BlinkMacSystemFont, "SF Pro Text", "Segoe UI", sans-serif;
    animation: statusbar-slide-up 0.35s ease-out;
  }

  @keyframes statusbar-slide-up {
    from { transform: translateY(100%); opacity: 0; }
    to   { transform: translateY(0); opacity: 1; }
  }

  :global(html.light) .status-bar {
    background: rgba(0, 0, 0, 0.02);
    border-top-color: rgba(0, 0, 0, 0.06);
    color: rgba(52, 59, 88, 0.4);
  }

  .status-item {
    display: flex;
    align-items: center;
    gap: 4px;
    white-space: nowrap;
    padding: 1px 6px;
    border-radius: 4px;
    transition: background 0.15s, color 0.15s;
  }

  .status-item:hover {
    background: rgba(255, 255, 255, 0.06);
    color: rgba(169, 177, 214, 0.7);
  }

  :global(html.light) .status-item:hover {
    background: rgba(0, 0, 0, 0.05);
    color: rgba(52, 59, 88, 0.7);
  }

  .status-text {
    font-size: 11px;
  }

  .status-detail {
    color: rgba(169, 177, 214, 0.25);
    font-size: 10px;
  }

  :global(html.light) .status-detail {
    color: rgba(52, 59, 88, 0.3);
  }

  .spacer {
    flex: 1;
  }

  .sync-dot {
    color: #9ece6a;
    font-size: 8px;
    line-height: 1;
  }

  .sync-dot.syncing {
    color: #e0af68;
    animation: pulse-sync 1s infinite;
  }

  @keyframes pulse-sync {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.3; }
  }

  .version {
    opacity: 0.5;
  }
</style>
