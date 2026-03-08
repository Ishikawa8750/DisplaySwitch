<script lang="ts">
  import type { Profile } from "./types";
  import { t } from "./i18n";

  let {
    profiles,
    activeProfileId,
    onactivate,
  }: {
    profiles: Profile[];
    activeProfileId: string | null;
    onactivate: (profile: Profile) => void;
  } = $props();

  const defaultIcons: Record<string, string> = {
    work: "💼",
    gaming: "🎮",
    presentation: "📊",
    movie: "🎬",
    custom: "⚙",
  };

  function iconFor(profile: Profile): string {
    return profile.icon ?? defaultIcons[profile.category] ?? "📌";
  }
</script>

{#if profiles.length > 0}
  <div class="profile-list">
    {#each profiles as profile (profile.id)}
      <button
        class="profile-item"
        class:active={profile.id === activeProfileId}
        onclick={() => onactivate(profile)}
        title={profile.description ?? profile.name}
      >
        <span class="item-icon">{iconFor(profile)}</span>
        <span class="item-label">{profile.name}</span>
        {#if profile.id === activeProfileId}
          <span class="item-badge">{t("profile.active")}</span>
        {/if}
      </button>
    {/each}
  </div>
{/if}

<style>
  .profile-list {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }

  .profile-item {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 8px 14px;
    border: none;
    border-radius: 10px;
    background: transparent;
    color: rgba(169, 177, 214, 0.7);
    cursor: pointer;
    font-size: 12px;
    font-family: inherit;
    transition: background 0.15s, color 0.15s;
    white-space: nowrap;
    text-align: left;
    width: 100%;
  }

  .profile-item:hover {
    background: rgba(255, 255, 255, 0.05);
    color: rgba(169, 177, 214, 0.9);
  }

  .profile-item.active {
    background: rgba(122, 162, 247, 0.1);
    color: #e1e4f0;
    border-left: 3px solid #7aa2f7;
    padding-left: 11px;
  }

  :global(html.light) .profile-item {
    color: rgba(52, 59, 88, 0.7);
  }

  :global(html.light) .profile-item:hover {
    background: rgba(0, 0, 0, 0.04);
    color: rgba(52, 59, 88, 0.9);
  }

  :global(html.light) .profile-item.active {
    background: rgba(52, 84, 138, 0.08);
    color: #1a1b2e;
    border-left-color: #34548a;
  }

  .item-icon {
    font-size: 14px;
    width: 20px;
    text-align: center;
    flex-shrink: 0;
  }

  .item-label {
    font-weight: 500;
    flex: 1;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .item-badge {
    font-size: 9px;
    padding: 2px 6px;
    border-radius: 4px;
    background: rgba(122, 162, 247, 0.15);
    color: #7aa2f7;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }

  :global(html.light) .item-badge {
    background: rgba(52, 84, 138, 0.1);
    color: #34548a;
  }
</style>
