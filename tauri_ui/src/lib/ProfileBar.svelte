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
    gap: 1px;
  }

  .profile-item {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 6px 12px;
    border: none;
    border-radius: 0;
    background: transparent;
    color: #a9b1d6;
    cursor: pointer;
    font-size: 12px;
    font-family: inherit;
    transition: background 0.12s;
    white-space: nowrap;
    text-align: left;
    width: 100%;
  }

  .profile-item:hover {
    background: #24283b;
  }

  .profile-item.active {
    background: #24283b;
    color: #c0caf5;
    border-left: 2px solid #7aa2f7;
    padding-left: 10px;
  }

  :global(html.light) .profile-item {
    color: #343b58;
  }

  :global(html.light) .profile-item:hover {
    background: #c4c5ca;
  }

  :global(html.light) .profile-item.active {
    background: #c4c5ca;
    color: #343b58;
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
    padding: 1px 5px;
    border-radius: 2px;
    background: rgba(122, 162, 247, 0.2);
    color: #7aa2f7;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }

  :global(html.light) .item-badge {
    background: rgba(52, 84, 138, 0.12);
    color: #34548a;
  }
</style>
