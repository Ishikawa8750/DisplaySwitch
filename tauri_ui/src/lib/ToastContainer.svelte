<script lang="ts">
  import { getToasts, dismissToast, onToastsChange, type ToastMessage } from "./toast.svelte";

  let toasts: ToastMessage[] = $state(getToasts());

  $effect(() => {
    const unsub = onToastsChange(() => {
      toasts = getToasts();
    });
    return unsub;
  });
</script>

{#if toasts.length > 0}
  <div class="toast-container">
    {#each toasts as t (t.id)}
      <div class="toast toast-{t.type}" role="alert">
        <span class="toast-icon">
          {#if t.type === "success"}✓
          {:else if t.type === "error"}✕
          {:else if t.type === "warning"}⚠
          {:else}ℹ
          {/if}
        </span>
        <span class="toast-text">{t.text}</span>
        <button class="toast-close" onclick={() => dismissToast(t.id)}>×</button>
      </div>
    {/each}
  </div>
{/if}

<style>
  .toast-container {
    position: fixed;
    bottom: 30px; /* above statusbar */
    right: 12px;
    z-index: 9999;
    display: flex;
    flex-direction: column-reverse;
    gap: 4px;
    max-width: 340px;
    pointer-events: none;
  }

  .toast {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 6px 12px;
    border-radius: 3px;
    font-size: 12px;
    font-family: "Segoe UI", -apple-system, sans-serif;
    color: #c0caf5;
    background: #24283b;
    border: 1px solid #232433;
    box-shadow: 0 2px 8px rgba(0, 0, 0, 0.3);
    animation: toast-in 0.15s ease-out;
    pointer-events: auto;
  }

  @keyframes toast-in {
    from { opacity: 0; transform: translateY(8px); }
    to   { opacity: 1; transform: translateY(0); }
  }

  .toast-info    { border-left: 3px solid #7aa2f7; }
  .toast-success { border-left: 3px solid #9ece6a; }
  .toast-warning { border-left: 3px solid #e0af68; color: #e0af68; }
  .toast-error   { border-left: 3px solid #f7768e; color: #f7768e; }

  :global(html.light) .toast {
    background: #e9e9ec;
    border-color: #c4c5ca;
    color: #343b58;
  }
  :global(html.light) .toast-info    { border-left-color: #34548a; }
  :global(html.light) .toast-success { border-left-color: #485e30; }
  :global(html.light) .toast-warning { border-left-color: #8f5e15; color: #8f5e15; }
  :global(html.light) .toast-error   { border-left-color: #8c4351; color: #8c4351; }

  .toast-icon {
    font-size: 12px;
    flex-shrink: 0;
    width: 16px;
    text-align: center;
    opacity: 0.8;
  }

  .toast-text {
    flex: 1;
  }

  .toast-close {
    background: none;
    border: none;
    color: inherit;
    font-size: 14px;
    cursor: pointer;
    padding: 0 2px;
    opacity: 0.5;
    line-height: 1;
  }
  .toast-close:hover {
    opacity: 1;
  }
</style>
