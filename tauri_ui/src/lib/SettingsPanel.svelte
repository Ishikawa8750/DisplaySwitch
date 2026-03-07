<script lang="ts">
  import type { AppConfig, HotkeyBinding, Preset, PresetAction, DisplayInfo, HotkeyActionType } from "./types";
  import { t } from "./i18n";

  let {
    config: initialConfig,
    displays,
    onclose,
    onsave,
  }: {
    config: AppConfig;
    displays: DisplayInfo[];
    onclose: () => void;
    onsave: (config: AppConfig) => void;
  } = $props();

  // Local mutable copy (deep clone the prop to avoid mutating parent state)
  let localConfig = $state<AppConfig>(JSON.parse(JSON.stringify(initialConfig)));
  let activeTab = $state<"general" | "hotkeys" | "presets">("general");
  let autostartLoading = $state(false);
  let statusMsg = $state("");
  let updateChecking = $state(false);
  let updateStatusMsg = $state("");

  // ── Check for Updates ──────────────────────────────────────────────────
  async function checkForUpdate() {
    updateChecking = true;
    updateStatusMsg = "";
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const version = await invoke<string | null>("check_for_update");
      if (version) {
        updateStatusMsg = t("updater.available", version);
      } else {
        updateStatusMsg = t("updater.up_to_date");
      }
    } catch (e: any) {
      updateStatusMsg = `⚠ ${e?.message ?? e}`;
    }
    updateChecking = false;
    setTimeout(() => (updateStatusMsg = ""), 8000);
  }

  // ── Autostart ──────────────────────────────────────────────────────────
  async function toggleAutostart() {
    autostartLoading = true;
    try {
      const autostart = await import("@tauri-apps/plugin-autostart");
      if (localConfig.autostart) {
        await autostart.disable();
        localConfig.autostart = false;
      } else {
        await autostart.enable();
        localConfig.autostart = true;
      }
      statusMsg = localConfig.autostart ? "✓ Autostart enabled" : "✓ Autostart disabled";
    } catch (e: any) {
      statusMsg = `⚠ Autostart: ${e?.message ?? e}`;
    }
    autostartLoading = false;
    setTimeout(() => (statusMsg = ""), 3000);
  }

  // ── Check actual autostart state on mount ──────────────────────────────
  $effect(() => {
    import("@tauri-apps/plugin-autostart").then(async (mod) => {
      try {
        localConfig.autostart = await mod.isEnabled();
      } catch {
        // Ignore
      }
    });
  });

  // ── Hotkeys ────────────────────────────────────────────────────────────
  let editingHotkeyId = $state<string | null>(null);
  let recordingShortcut = $state(false);
  let recordedKeys = $state("");

  function startRecording(id: string) {
    editingHotkeyId = id;
    recordingShortcut = true;
    recordedKeys = "Press keys…";
  }

  function handleKeyDown(e: KeyboardEvent) {
    if (!recordingShortcut || !editingHotkeyId) return;
    e.preventDefault();
    e.stopPropagation();

    // Build shortcut string from modifiers + key
    const parts: string[] = [];
    if (e.ctrlKey) parts.push("Ctrl");
    if (e.altKey) parts.push("Alt");
    if (e.shiftKey) parts.push("Shift");
    if (e.metaKey) parts.push("Super");

    // Ignore standalone modifier keys
    const key = e.key;
    if (["Control", "Alt", "Shift", "Meta"].includes(key)) {
      recordedKeys = parts.join("+") + "+…";
      return;
    }

    // Map special keys
    let keyName = key;
    if (key === "ArrowUp") keyName = "Up";
    else if (key === "ArrowDown") keyName = "Down";
    else if (key === "ArrowLeft") keyName = "Left";
    else if (key === "ArrowRight") keyName = "Right";
    else if (key.length === 1) keyName = key.toUpperCase();

    parts.push(keyName);
    const shortcut = parts.join("+");

    // Apply to binding
    const idx = localConfig.hotkeys.findIndex((h) => h.id === editingHotkeyId);
    if (idx >= 0) {
      localConfig.hotkeys[idx] = { ...localConfig.hotkeys[idx], shortcut };
    }

    recordingShortcut = false;
    editingHotkeyId = null;
    recordedKeys = "";
  }

  function removeHotkey(id: string) {
    localConfig.hotkeys = localConfig.hotkeys.filter((h) => h.id !== id);
  }

  function addHotkey() {
    const id = "hk_" + Date.now();
    localConfig.hotkeys = [
      ...localConfig.hotkeys,
      {
        id,
        shortcut: "",
        action: "brightness_up" as HotkeyActionType,
        value: 10,
        label: "New Hotkey",
      },
    ];
  }

  // ── Presets ────────────────────────────────────────────────────────────
  let editingPresetId = $state<string | null>(null);

  function addPreset() {
    const id = "preset_" + Date.now();
    const actions: PresetAction[] = displays.map((d) => ({
      display_index: d.display_index,
      display_name: d.name,
      brightness: d.brightness >= 0 ? d.brightness : undefined,
      input_code: !d.is_internal && d.current_input > 0 ? d.current_input : undefined,
    }));
    localConfig.presets = [
      ...localConfig.presets,
      { id, name: "New Preset", actions },
    ];
    editingPresetId = id;
  }

  function removePreset(id: string) {
    localConfig.presets = localConfig.presets.filter((p) => p.id !== id);
    if (editingPresetId === id) editingPresetId = null;
  }

  function saveAndClose() {
    onsave(localConfig);
    onclose();
  }

  const actionLabels: Record<HotkeyActionType, string> = {
    brightness_up: t("hotkey.action.brightness_up"),
    brightness_down: t("hotkey.action.brightness_down"),
    switch_input: t("hotkey.action.switch_input"),
    apply_preset: t("hotkey.action.apply_preset"),
    apply_profile: t("profile.activate"),
    refresh: t("hotkey.action.refresh"),
  };

  const inputNames: Record<number, string> = {
    1: "VGA-1", 2: "VGA-2", 3: "DVI-1", 4: "DVI-2",
    15: "DP-1", 16: "DP-2", 17: "HDMI-1", 18: "HDMI-2", 27: "USB-C",
  };
</script>

<!-- svelte-ignore a11y_no_noninteractive_element_interactions -->
<!-- svelte-ignore a11y_interactive_supports_focus -->
<div class="overlay" role="dialog" aria-modal="true" tabindex="-1" onkeydown={handleKeyDown}>
  <div class="panel">
    <header>
      <h2>⚙ {t("settings.title")}</h2>
      <button class="close-btn" onclick={onclose}>✕</button>
    </header>

    <!-- Tabs -->
    <nav class="tabs">
      <button class:active={activeTab === "general"} onclick={() => (activeTab = "general")}>
        {t("settings.tab.general")}
      </button>
      <button class:active={activeTab === "hotkeys"} onclick={() => (activeTab = "hotkeys")}>
        {t("settings.tab.hotkeys")}
      </button>
      <button class:active={activeTab === "presets"} onclick={() => (activeTab = "presets")}>
        {t("settings.tab.presets")}
      </button>
    </nav>

    <div class="content">
      <!-- ═══ General Tab ═══ -->
      {#if activeTab === "general"}
        <section>
          <div class="setting-row">
            <div>
              <strong>{t("settings.autostart")}</strong>
              <p class="hint">Launch DisplaySwitch when you log in</p>
            </div>
            <button
              class="toggle"
              class:on={localConfig.autostart}
              onclick={toggleAutostart}
              disabled={autostartLoading}
            >
              {localConfig.autostart ? "ON" : "OFF"}
            </button>
          </div>

          <div class="setting-row">
            <div>
              <strong>{t("settings.sync_interval")}</strong>
              <p class="hint">Brightness polling interval (ms)</p>
            </div>
            <select
              value={String(localConfig.sync_interval_ms)}
              onchange={(e) => {
                localConfig.sync_interval_ms = Number(e.currentTarget.value);
              }}
            >
              <option value="2000">2s</option>
              <option value="5000">5s (default)</option>
              <option value="10000">10s</option>
              <option value="30000">30s</option>
            </select>
          </div>

          <div class="setting-row">
            <div>
              <strong>{t("settings.theme")}</strong>
              <p class="hint">Dark / Light / System</p>
            </div>
            <select
              value={localConfig.theme}
              onchange={(e) => {
                localConfig.theme = e.currentTarget.value as AppConfig["theme"];
              }}
            >
              <option value="dark">{t("settings.theme.dark")}</option>
              <option value="light">{t("settings.theme.light")}</option>
              <option value="system">{t("settings.theme.system")}</option>
            </select>
          </div>

          <div class="setting-row">
            <div>
              <strong>{t("settings.language")}</strong>
              <p class="hint">Interface language</p>
            </div>
            <select
              value={localConfig.locale}
              onchange={(e) => {
                localConfig.locale = e.currentTarget.value as AppConfig["locale"];
              }}
            >
              <option value="auto">{t("settings.lang.auto")}</option>
              <option value="en">{t("settings.lang.en")}</option>
              <option value="zh">{t("settings.lang.zh")}</option>
            </select>
          </div>

          <!-- ALS Auto Brightness -->
          <div class="setting-row">
            <div>
              <strong>{t("als.auto_brightness")}</strong>
              <p class="hint">{localConfig.als_enabled ? t("als.enabled") : t("als.disabled")}</p>
            </div>
            <button
              class="toggle"
              class:on={localConfig.als_enabled}
              onclick={() => { localConfig.als_enabled = !localConfig.als_enabled; }}
            >
              {localConfig.als_enabled ? "ON" : "OFF"}
            </button>
          </div>

          {#if localConfig.als_enabled}
            <div class="setting-row">
              <div>
                <strong>{t("als.interval")}</strong>
                <p class="hint">ms</p>
              </div>
              <select
                value={String(localConfig.als_interval_ms)}
                onchange={(e) => {
                  localConfig.als_interval_ms = Number(e.currentTarget.value);
                }}
              >
                <option value="5000">5s</option>
                <option value="10000">10s (default)</option>
                <option value="30000">30s</option>
                <option value="60000">60s</option>
              </select>
            </div>
          {/if}

          <!-- Check for Updates -->
          <div class="setting-row">
            <div>
              <strong>{t("updater.check")}</strong>
              <p class="hint">{updateStatusMsg}</p>
            </div>
            <button onclick={checkForUpdate} disabled={updateChecking}>
              {updateChecking ? t("updater.checking") : t("updater.check")}
            </button>
          </div>
        </section>

      <!-- ═══ Hotkeys Tab ═══ -->
      {:else if activeTab === "hotkeys"}
        <section>
          <p class="hint" style="margin-bottom: 0.8rem">
            Global keyboard shortcuts work even when the window is hidden.
          </p>

          {#each localConfig.hotkeys as hk, i (hk.id)}
            <div class="hotkey-row">
              <div class="hk-shortcut">
                <button
                  class="shortcut-btn"
                  class:recording={editingHotkeyId === hk.id}
                  onclick={() => startRecording(hk.id)}
                >
                  {editingHotkeyId === hk.id ? recordedKeys : (hk.shortcut || "Click to set")}
                </button>
              </div>
              <div class="hk-action">
                <select
                  value={hk.action}
                  onchange={(e) => {
                    localConfig.hotkeys[i] = {
                      ...hk,
                      action: e.currentTarget.value as HotkeyActionType,
                    };
                  }}
                >
                  {#each Object.entries(actionLabels) as [key, label]}
                    <option value={key}>{label}</option>
                  {/each}
                </select>
              </div>
              <div class="hk-value">
                {#if hk.action === "brightness_up" || hk.action === "brightness_down"}
                  <input
                    type="number"
                    min="1"
                    max="50"
                    value={hk.value ?? 10}
                    onchange={(e) => {
                      localConfig.hotkeys[i] = {
                        ...hk,
                        value: Number(e.currentTarget.value),
                      };
                    }}
                  />
                  <span class="unit">%</span>
                {:else if hk.action === "apply_preset"}
                  <select
                    value={String(hk.value ?? 0)}
                    onchange={(e) => {
                      localConfig.hotkeys[i] = {
                        ...hk,
                        value: Number(e.currentTarget.value),
                      };
                    }}
                  >
                    {#each localConfig.presets as preset, pi}
                      <option value={String(pi)}>{preset.name}</option>
                    {/each}
                    {#if localConfig.presets.length === 0}
                      <option value="0">(no presets)</option>
                    {/if}
                  </select>
                {/if}
              </div>
              <button class="remove-btn" onclick={() => removeHotkey(hk.id)}>✕</button>
            </div>
          {/each}

          <button class="add-btn" onclick={addHotkey}>{t("hotkey.add")}</button>
        </section>

      <!-- ═══ Presets Tab ═══ -->
      {:else if activeTab === "presets"}
        <section>
          <p class="hint" style="margin-bottom: 0.8rem">
            Presets save brightness & input source combos to apply with one click or hotkey.
          </p>

          {#each localConfig.presets as preset, pi (preset.id)}
            <div class="preset-card">
              <div class="preset-header">
                <input
                  class="preset-name"
                  type="text"
                  value={preset.name}
                  onchange={(e) => {
                    localConfig.presets[pi] = { ...preset, name: e.currentTarget.value };
                  }}
                />
                <button class="remove-btn" onclick={() => removePreset(preset.id)}>✕</button>
              </div>

              {#if editingPresetId === preset.id}
                <div class="preset-actions">
                  {#each preset.actions as act, ai}
                    <div class="action-row">
                      <span class="act-display">{act.display_name}</span>
                      <label>
                        Brightness
                        <input
                          type="number"
                          min="0"
                          max="100"
                          value={act.brightness ?? ""}
                          placeholder="—"
                          onchange={(e) => {
                            const val = e.currentTarget.value;
                            const newAct = { ...act, brightness: val ? Number(val) : undefined };
                            const newActions = [...preset.actions];
                            newActions[ai] = newAct;
                            localConfig.presets[pi] = { ...preset, actions: newActions };
                          }}
                        />
                      </label>
                      <label>
                        Input
                        <select
                          value={String(act.input_code ?? "")}
                          onchange={(e) => {
                            const val = e.currentTarget.value;
                            const newAct = { ...act, input_code: val ? Number(val) : undefined };
                            const newActions = [...preset.actions];
                            newActions[ai] = newAct;
                            localConfig.presets[pi] = { ...preset, actions: newActions };
                          }}
                        >
                          <option value="">—</option>
                          {#each Object.entries(inputNames) as [code, name]}
                            <option value={code}>{name}</option>
                          {/each}
                        </select>
                      </label>
                    </div>
                  {/each}
                </div>
                <button class="collapse-btn" onclick={() => (editingPresetId = null)}>
                  ▲ Collapse
                </button>
              {:else}
                <button class="collapse-btn" onclick={() => (editingPresetId = preset.id)}>
                  ▼ Edit actions ({preset.actions.length} displays)
                </button>
              {/if}
            </div>
          {/each}

          <button class="add-btn" onclick={addPreset}>{t("preset.add")}</button>
        </section>
      {/if}
    </div>

    {#if statusMsg}
      <div class="status">{statusMsg}</div>
    {/if}

    <footer>
      <button class="cancel-btn" onclick={onclose}>{t("settings.cancel")}</button>
      <button class="save-btn" onclick={saveAndClose}>{t("settings.save")}</button>
    </footer>
  </div>
</div>

<style>
  .overlay {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.5);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 1000;
  }

  .panel {
    background: #1a1b26;
    border: 1px solid #232433;
    border-radius: 4px;
    width: 560px;
    max-height: 80vh;
    display: flex;
    flex-direction: column;
    overflow: hidden;
    box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
  }

  header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 10px 16px;
    border-bottom: 1px solid #232433;
  }

  h2 {
    margin: 0;
    font-size: 13px;
    font-weight: 600;
    color: #c0caf5;
  }

  .close-btn {
    background: none;
    border: none;
    color: #565a89;
    font-size: 16px;
    cursor: pointer;
    padding: 2px 4px;
    line-height: 1;
  }
  .close-btn:hover { color: #c0caf5; }

  .tabs {
    display: flex;
    border-bottom: 1px solid #232433;
    padding: 0 12px;
  }
  .tabs button {
    background: none;
    border: none;
    border-bottom: 2px solid transparent;
    color: #565a89;
    padding: 8px 14px;
    cursor: pointer;
    font-size: 12px;
    font-family: inherit;
    transition: all 0.12s;
  }
  .tabs button:hover { color: #a9b1d6; }
  .tabs button.active {
    color: #7aa2f7;
    border-bottom-color: #7aa2f7;
  }

  .content {
    flex: 1;
    overflow-y: auto;
    padding: 12px 16px;
  }

  section { display: flex; flex-direction: column; gap: 4px; }

  .setting-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 8px 0;
    border-bottom: 1px solid #232433;
  }
  .setting-row strong { color: #a9b1d6; font-size: 12px; font-weight: 500; }

  .hint { color: #3b3f5c; font-size: 11px; margin: 2px 0 0; }

  .toggle {
    padding: 3px 12px;
    border: 1px solid #24283b;
    border-radius: 3px;
    background: #24283b;
    color: #565a89;
    cursor: pointer;
    font-size: 11px;
    font-family: inherit;
    min-width: 48px;
    transition: all 0.15s;
  }
  .toggle.on {
    background: #34548a;
    border-color: #7aa2f7;
    color: #c0caf5;
  }

  select {
    background: #24283b;
    border: 1px solid #232433;
    border-radius: 3px;
    color: #a9b1d6;
    padding: 3px 6px;
    font-size: 12px;
    font-family: inherit;
  }

  /* ── Hotkeys ── */
  .hotkey-row {
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 6px 0;
    border-bottom: 1px solid #232433;
  }
  .hk-shortcut { flex: 0 0 130px; }
  .hk-action { flex: 1; }
  .hk-value {
    flex: 0 0 72px;
    display: flex;
    align-items: center;
    gap: 3px;
  }
  .hk-value input {
    width: 44px;
    background: #24283b;
    border: 1px solid #232433;
    border-radius: 3px;
    color: #a9b1d6;
    padding: 2px 4px;
    font-size: 11px;
    font-family: inherit;
  }
  .unit { color: #3b3f5c; font-size: 10px; }

  .shortcut-btn {
    width: 100%;
    padding: 3px 8px;
    border: 1px solid #24283b;
    border-radius: 3px;
    background: #16161e;
    color: #a9b1d6;
    cursor: pointer;
    font-family: "Cascadia Code", "Consolas", monospace;
    font-size: 11px;
    text-align: center;
  }
  .shortcut-btn:hover { border-color: #7aa2f7; }
  .shortcut-btn.recording {
    border-color: #e0af68;
    color: #e0af68;
    animation: pulse 1s infinite;
  }

  @keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.5; }
  }

  .remove-btn {
    background: none;
    border: none;
    color: #3b3f5c;
    cursor: pointer;
    font-size: 14px;
    padding: 2px 4px;
    font-family: inherit;
  }
  .remove-btn:hover { color: #f7768e; }

  .add-btn {
    margin-top: 6px;
    padding: 4px 12px;
    border: 1px dashed #232433;
    border-radius: 3px;
    background: transparent;
    color: #565a89;
    cursor: pointer;
    font-size: 11px;
    font-family: inherit;
  }
  .add-btn:hover { border-color: #7aa2f7; color: #7aa2f7; }

  /* ── Presets ── */
  .preset-card {
    background: #16161e;
    border: 1px solid #232433;
    border-radius: 3px;
    padding: 10px;
  }
  .preset-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }
  .preset-name {
    background: transparent;
    border: none;
    border-bottom: 1px solid #232433;
    color: #c0caf5;
    font-size: 13px;
    font-weight: 600;
    font-family: inherit;
    padding: 2px 0;
    flex: 1;
    margin-right: 8px;
  }
  .preset-name:focus { border-bottom-color: #7aa2f7; outline: none; }

  .preset-actions {
    margin-top: 8px;
    display: flex;
    flex-direction: column;
    gap: 4px;
  }
  .action-row {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 12px;
  }
  .act-display {
    flex: 0 0 110px;
    color: #565a89;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .action-row label {
    display: flex;
    align-items: center;
    gap: 4px;
    color: #565a89;
    font-size: 11px;
  }
  .action-row input {
    width: 44px;
    background: #24283b;
    border: 1px solid #232433;
    border-radius: 3px;
    color: #a9b1d6;
    padding: 2px;
    font-size: 11px;
    font-family: inherit;
  }
  .action-row select {
    font-size: 11px;
  }

  .collapse-btn {
    margin-top: 4px;
    background: none;
    border: none;
    color: #3b3f5c;
    cursor: pointer;
    font-size: 11px;
    font-family: inherit;
    padding: 2px 0;
  }
  .collapse-btn:hover { color: #7aa2f7; }

  .status {
    padding: 6px 16px;
    font-size: 11px;
    color: #e0af68;
    background: rgba(224, 175, 104, 0.06);
  }

  footer {
    display: flex;
    justify-content: flex-end;
    gap: 8px;
    padding: 8px 16px;
    border-top: 1px solid #232433;
  }
  .cancel-btn {
    padding: 4px 14px;
    border: 1px solid #232433;
    border-radius: 3px;
    background: #24283b;
    color: #565a89;
    cursor: pointer;
    font-size: 12px;
    font-family: inherit;
  }
  .cancel-btn:hover { background: #2a2e47; }

  .save-btn {
    padding: 4px 14px;
    border: 1px solid #34548a;
    border-radius: 3px;
    background: #34548a;
    color: #c0caf5;
    cursor: pointer;
    font-size: 12px;
    font-family: inherit;
  }
  .save-btn:hover { background: #3d5fa0; }

  /* ── Light theme ── */
  :global(html.light) .panel {
    background: #d5d6db;
    border-color: #c4c5ca;
  }
  :global(html.light) header {
    border-bottom-color: #c4c5ca;
  }
  :global(html.light) h2 { color: #343b58; }
  :global(html.light) .close-btn { color: #9ca0b9; }
  :global(html.light) .close-btn:hover { color: #343b58; }
  :global(html.light) .tabs { border-bottom-color: #c4c5ca; }
  :global(html.light) .tabs button { color: #8990b3; }
  :global(html.light) .tabs button:hover { color: #343b58; }
  :global(html.light) .tabs button.active {
    color: #34548a;
    border-bottom-color: #34548a;
  }
  :global(html.light) .setting-row { border-bottom-color: #c4c5ca; }
  :global(html.light) .setting-row strong { color: #343b58; }
  :global(html.light) .hint { color: #9ca0b9; }
  :global(html.light) .toggle {
    background: #c4c5ca;
    border-color: #b8b9be;
    color: #8990b3;
  }
  :global(html.light) .toggle.on {
    background: #34548a;
    border-color: #2e4a7a;
    color: #d5d6db;
  }
  :global(html.light) select {
    background: #c4c5ca;
    border-color: #b8b9be;
    color: #343b58;
  }
  :global(html.light) .shortcut-btn {
    background: #c4c5ca;
    border-color: #b8b9be;
    color: #343b58;
  }
  :global(html.light) .hk-value input {
    background: #c4c5ca;
    border-color: #b8b9be;
    color: #343b58;
  }
  :global(html.light) .hotkey-row { border-bottom-color: #c4c5ca; }
  :global(html.light) .preset-card {
    background: #c4c5ca;
    border-color: #b8b9be;
  }
  :global(html.light) .preset-name { color: #343b58; border-bottom-color: #b8b9be; }
  :global(html.light) .preset-name:focus { border-bottom-color: #34548a; }
  :global(html.light) .action-row input {
    background: #d5d6db;
    border-color: #b8b9be;
    color: #343b58;
  }
  :global(html.light) .add-btn { border-color: #b8b9be; color: #8990b3; }
  :global(html.light) .add-btn:hover { border-color: #34548a; color: #34548a; }
  :global(html.light) .cancel-btn {
    background: #c4c5ca;
    border-color: #b8b9be;
    color: #8990b3;
  }
  :global(html.light) .cancel-btn:hover { background: #b8b9be; }
  :global(html.light) footer { border-top-color: #c4c5ca; }
  :global(html.light) .status { color: #8f5e15; background: rgba(143, 94, 21, 0.06); }
</style>
