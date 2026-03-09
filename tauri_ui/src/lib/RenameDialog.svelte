<script lang="ts">
  import { t } from "./i18n";

  let {
    displayName,
    originalDisplayName,
    inputCodes,
    getInputName,
    getDefaultInputName,
    hasCustomName,
    customDisplayName,
    onInputNameChange,
    onDisplayNameChange,
    onclose,
  }: {
    displayName: string;
    originalDisplayName: string;
    inputCodes: number[];
    getInputName: (code: number) => string;
    getDefaultInputName: (code: number) => string;
    hasCustomName: (code: number) => boolean;
    customDisplayName: string | null;
    onInputNameChange?: (code: number, newName: string | null) => void;
    onDisplayNameChange?: (newName: string | null) => void;
    onclose: () => void;
  } = $props();

  let editValues = $state<Record<number, string>>({});
  let displayNameValue = $state("");

  // Initialize values ONCE on mount (untrack to prevent re-runs when parent state changes)
  let _initialized = false;
  $effect(() => {
    if (_initialized) return;
    _initialized = true;
    const vals: Record<number, string> = {};
    for (const code of inputCodes) {
      vals[code] = getInputName(code);
    }
    editValues = vals;
    displayNameValue = customDisplayName || originalDisplayName;
  });

  function saveInputName(code: number) {
    const trimmed = editValues[code]?.trim() ?? "";
    const defaultName = getDefaultInputName(code);
    if (!trimmed || trimmed === defaultName) {
      onInputNameChange?.(code, null);
    } else {
      onInputNameChange?.(code, trimmed);
    }
  }

  function resetInput(code: number) {
    onInputNameChange?.(code, null);
    editValues[code] = getDefaultInputName(code);
  }

  function resetAllInputs() {
    for (const code of inputCodes) {
      onInputNameChange?.(code, null);
      editValues[code] = getDefaultInputName(code);
    }
  }

  function saveDisplayName() {
    const trimmed = displayNameValue.trim();
    if (!trimmed || trimmed === originalDisplayName) {
      onDisplayNameChange?.(null);
    } else {
      onDisplayNameChange?.(trimmed);
    }
  }

  function resetDisplayName() {
    onDisplayNameChange?.(null);
    displayNameValue = originalDisplayName;
  }

  function saveAllAndClose() {
    saveDisplayName();
    for (const code of inputCodes) {
      saveInputName(code);
    }
    onclose();
  }

  function handleKeydown(e: KeyboardEvent) {
    if (e.key === "Escape") onclose();
  }
</script>

<!-- svelte-ignore a11y_no_static_element_interactions -->
<div class="overlay" onclick={onclose} onkeydown={handleKeydown}>
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div class="dialog" onclick={(e) => e.stopPropagation()}>
    <div class="dialog-header">
      <h3>✏️ {t("input.rename")}</h3>
      <button class="close-btn" onclick={onclose}>✕</button>
    </div>

    <!-- Display Name -->
    <div class="section">
      <div class="section-label">{displayName}</div>
      <div class="rename-row">
        <input
          type="text"
          class="rename-input"
          bind:value={displayNameValue}
          onkeydown={(e) => { if (e.key === 'Enter') saveDisplayName(); }}
          placeholder={originalDisplayName}
        />
        {#if customDisplayName}
          <button class="reset-btn" onclick={resetDisplayName} title={t("input.reset")}>↺</button>
        {/if}
      </div>
    </div>

    <!-- Input Sources -->
    {#if inputCodes.length > 0}
      <div class="section">
        <div class="section-label">{t("card.input")}</div>
        {#each inputCodes as code}
          <div class="rename-row">
            <span class="input-code">{getDefaultInputName(code)}</span>
            <input
              type="text"
              class="rename-input"
              bind:value={editValues[code]}
              onkeydown={(e) => { if (e.key === 'Enter') saveInputName(code); }}
              placeholder={getDefaultInputName(code)}
            />
            {#if hasCustomName(code)}
              <button class="reset-btn" onclick={() => resetInput(code)} title={t("input.reset")}>↺</button>
            {/if}
          </div>
        {/each}
        {#if Object.values(editValues).some((v, i) => {
          const code = inputCodes[i];
          return hasCustomName(code);
        })}
          <button class="reset-all-btn" onclick={resetAllInputs}>↺ {t("input.reset_all")}</button>
        {/if}
      </div>
    {/if}

    <!-- Actions -->
    <div class="dialog-actions">
      <button class="action-btn cancel" onclick={onclose}>Cancel</button>
      <button class="action-btn save" onclick={saveAllAndClose}>{t("input.save")}</button>
    </div>
  </div>
</div>

<style>
  .overlay {
    position: fixed;
    inset: 0;
    z-index: 10000;
    background: rgba(0, 0, 0, 0.5);
    display: flex;
    align-items: center;
    justify-content: center;
    animation: overlay-enter 0.15s ease-out;
  }
  @keyframes overlay-enter {
    from { opacity: 0; }
    to { opacity: 1; }
  }

  .dialog {
    background: rgba(30, 30, 46, 0.98);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 14px;
    padding: 20px;
    min-width: 320px;
    max-width: 420px;
    box-shadow: 0 20px 60px rgba(0, 0, 0, 0.5);
    animation: dialog-enter 0.2s ease-out;
  }
  @keyframes dialog-enter {
    from { opacity: 0; transform: scale(0.95) translateY(8px); }
    to { opacity: 1; transform: scale(1) translateY(0); }
  }

  .dialog-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 16px;
  }
  .dialog-header h3 {
    margin: 0;
    font-size: 15px;
    font-weight: 600;
    color: #e1e4f0;
  }
  .close-btn {
    background: transparent;
    border: none;
    color: rgba(169, 177, 214, 0.6);
    font-size: 14px;
    cursor: pointer;
    padding: 4px 8px;
    border-radius: 6px;
    transition: background 0.15s, color 0.15s;
  }
  .close-btn:hover {
    background: rgba(255, 255, 255, 0.08);
    color: #e1e4f0;
  }

  .section {
    margin-bottom: 16px;
  }
  .section-label {
    font-size: 11px;
    font-weight: 600;
    color: rgba(169, 177, 214, 0.5);
    text-transform: uppercase;
    letter-spacing: 0.05em;
    margin-bottom: 8px;
  }

  .rename-row {
    display: flex;
    align-items: center;
    gap: 6px;
    margin-bottom: 6px;
  }
  .input-code {
    font-size: 11px;
    color: rgba(169, 177, 214, 0.5);
    min-width: 60px;
    text-align: right;
    flex-shrink: 0;
  }
  .rename-input {
    flex: 1;
    padding: 6px 10px;
    font-size: 13px;
    font-family: inherit;
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 8px;
    background: rgba(255, 255, 255, 0.05);
    color: #e1e4f0;
    outline: none;
    transition: border-color 0.2s, box-shadow 0.2s;
  }
  .rename-input:focus {
    border-color: rgba(122, 162, 247, 0.5);
    box-shadow: 0 0 0 2px rgba(122, 162, 247, 0.12);
  }
  .rename-input::placeholder {
    color: rgba(169, 177, 214, 0.3);
  }

  .reset-btn {
    padding: 4px 8px;
    font-size: 13px;
    background: transparent;
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 6px;
    color: rgba(169, 177, 214, 0.6);
    cursor: pointer;
    transition: background 0.15s, color 0.15s;
    flex-shrink: 0;
  }
  .reset-btn:hover {
    background: rgba(247, 118, 142, 0.1);
    color: #f7768e;
  }

  .reset-all-btn {
    margin-top: 4px;
    padding: 5px 12px;
    font-size: 11px;
    background: transparent;
    border: 1px solid rgba(255, 255, 255, 0.06);
    border-radius: 6px;
    color: rgba(247, 118, 142, 0.7);
    cursor: pointer;
    font-family: inherit;
    transition: background 0.15s, color 0.15s;
  }
  .reset-all-btn:hover {
    background: rgba(247, 118, 142, 0.08);
    color: #f7768e;
  }

  .dialog-actions {
    display: flex;
    justify-content: flex-end;
    gap: 8px;
    padding-top: 12px;
    border-top: 1px solid rgba(255, 255, 255, 0.06);
  }
  .action-btn {
    padding: 6px 16px;
    font-size: 12px;
    font-weight: 500;
    border-radius: 8px;
    border: none;
    cursor: pointer;
    font-family: inherit;
    transition: background 0.15s;
  }
  .action-btn.cancel {
    background: rgba(255, 255, 255, 0.06);
    color: rgba(169, 177, 214, 0.8);
  }
  .action-btn.cancel:hover {
    background: rgba(255, 255, 255, 0.1);
  }
  .action-btn.save {
    background: rgba(122, 162, 247, 0.2);
    color: #7aa2f7;
  }
  .action-btn.save:hover {
    background: rgba(122, 162, 247, 0.3);
  }

  /* Light theme */
  :global(html.light) .dialog {
    background: rgba(255, 255, 255, 0.98);
    border-color: rgba(0, 0, 0, 0.1);
  }
  :global(html.light) .dialog-header h3 { color: #1a1b26; }
  :global(html.light) .close-btn { color: rgba(0, 0, 0, 0.4); }
  :global(html.light) .close-btn:hover { background: rgba(0, 0, 0, 0.06); color: #1a1b26; }
  :global(html.light) .section-label { color: rgba(0, 0, 0, 0.4); }
  :global(html.light) .input-code { color: rgba(0, 0, 0, 0.4); }
  :global(html.light) .rename-input {
    background: rgba(0, 0, 0, 0.03);
    border-color: rgba(0, 0, 0, 0.1);
    color: #1a1b26;
  }
  :global(html.light) .rename-input::placeholder { color: rgba(0, 0, 0, 0.3); }
  :global(html.light) .action-btn.cancel {
    background: rgba(0, 0, 0, 0.05);
    color: rgba(0, 0, 0, 0.6);
  }
</style>
