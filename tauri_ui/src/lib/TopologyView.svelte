<!--
  TopologyView — Phase 7
  Visual display topology: GPU → connection → display.
  Pure SVG (no D3.js dependency). Shows Thunderbolt/USB-C/DP/HDMI chains.
-->
<script lang="ts">
  import type { DisplayInfo } from "./types";
  import { t } from "./i18n";

  let { displays }: { displays: DisplayInfo[] } = $props();

  // Group displays by GPU
  let gpuGroups = $derived.by(() => {
    const groups = new Map<string, DisplayInfo[]>();
    for (const d of displays) {
      const key = d.gpu?.formatted_name ?? d.gpu?.name ?? "Unknown GPU";
      if (!groups.has(key)) groups.set(key, []);
      groups.get(key)!.push(d);
    }
    return groups;
  });

  function connIcon(type: string): string {
    const t = type.toLowerCase();
    if (t.includes("thunderbolt") || t.includes("usb-c") || t.includes("usb4")) return "⚡";
    if (t.includes("dp") || t.includes("displayport")) return "🔌";
    if (t.includes("hdmi")) return "📺";
    if (t.includes("edp") || t.includes("lvds")) return "💻";
    return "🔗";
  }

  function connColor(type: string): string {
    const t = type.toLowerCase();
    if (t.includes("thunderbolt") || t.includes("usb-c") || t.includes("usb4")) return "#e0af68";
    if (t.includes("dp") || t.includes("displayport")) return "#9ece6a";
    if (t.includes("hdmi")) return "#7aa2f7";
    if (t.includes("edp") || t.includes("lvds")) return "#bb9af7";
    return "#565a89";
  }

  function bwLabel(d: DisplayInfo): string {
    if (!d.bandwidth?.bandwidth_str) return "";
    return d.bandwidth.bandwidth_str;
  }

  // SVG layout constants
  const GPU_W = 140;
  const GPU_H = 48;
  const DISPLAY_W = 180;
  const DISPLAY_H = 56;
  const GAP_X = 160;
  const GAP_Y = 20;
  const MARGIN = 24;

  let svgHeight = $derived.by(() => {
    let maxGroupDisplays = 0;
    for (const [, disps] of gpuGroups) {
      if (disps.length > maxGroupDisplays) maxGroupDisplays = disps.length;
    }
    return Math.max(200, MARGIN * 2 + maxGroupDisplays * (DISPLAY_H + GAP_Y));
  });

  let svgWidth = $derived(MARGIN * 2 + GPU_W + GAP_X + DISPLAY_W + 40);
</script>

{#if displays.length > 0}
  <div class="topology-container">
    {#each [...gpuGroups] as [gpuName, gpuDisplays], gi}
      <div class="gpu-group">
        <svg
          width={svgWidth}
          height={MARGIN * 2 + gpuDisplays.length * (DISPLAY_H + GAP_Y) - GAP_Y}
          class="topology-svg"
        >
          {#if true}
          {@const gpuX = MARGIN}
          {@const gpuY = MARGIN + ((gpuDisplays.length * (DISPLAY_H + GAP_Y) - GAP_Y) / 2) - GPU_H / 2}

          <!-- GPU Node -->
          <rect
            x={gpuX} y={gpuY}
            width={GPU_W} height={GPU_H}
            rx="3" fill="#24283b" stroke="#7aa2f7" stroke-width="1"
          />
          <text x={gpuX + GPU_W / 2} y={gpuY + 18} text-anchor="middle" fill="#c0caf5" font-size="10" font-weight="600">
            GPU
          </text>
          <text x={gpuX + GPU_W / 2} y={gpuY + 34} text-anchor="middle" fill="#565a89" font-size="9">
            {gpuName.length > 18 ? gpuName.slice(0, 18) + "…" : gpuName}
          </text>

          <!-- Display Nodes + Connection Lines -->
          {#each gpuDisplays as disp, di}
            {@const dx = MARGIN + GPU_W + GAP_X}
            {@const dy = MARGIN + di * (DISPLAY_H + GAP_Y)}
            {@const color = connColor(disp.connection_type)}
            {@const midX = (gpuX + GPU_W + dx) / 2}
            {@const midY = (gpuY + GPU_H / 2 + dy + DISPLAY_H / 2) / 2}

            <!-- Connection line -->
            <line
              x1={gpuX + GPU_W}
              y1={gpuY + GPU_H / 2}
              x2={dx}
              y2={dy + DISPLAY_H / 2}
              stroke={color}
              stroke-width="1.5"
              stroke-dasharray={disp.is_internal ? "none" : "6,3"}
            />

            <!-- Connection label -->
            <text x={midX} y={midY - 6} text-anchor="middle" fill={color} font-size="9" font-weight="500">
              {connIcon(disp.connection_type)} {disp.hdmi_version ?? disp.connection_type}
            </text>
            {#if bwLabel(disp)}
              <text x={midX} y={midY + 6} text-anchor="middle" fill="#565a89" font-size="8">
                {bwLabel(disp)}
              </text>
            {/if}

            <!-- Display node -->
            <rect
              x={dx} y={dy}
              width={DISPLAY_W} height={DISPLAY_H}
              rx="3" fill="#1a1b26" stroke={color} stroke-width="1"
            />
            <text x={dx + 10} y={dy + 18} fill="#c0caf5" font-size="10" font-weight="500">
              {disp.is_internal ? "▪" : "▫"} {disp.name.length > 16 ? disp.name.slice(0, 16) + "…" : disp.name}
            </text>
            <text x={dx + 10} y={dy + 32} fill="#a9b1d6" font-size="9">
              {disp.resolution_str} | {disp.refresh_rate.toFixed(0)}Hz
            </text>
            <text x={dx + 10} y={dy + 46} fill="#565a89" font-size="8">
              {disp.manufacturer_id} {disp.product_code}
              {#if disp.brightness >= 0} | {disp.brightness}%{/if}
            </text>
          {/each}
          {/if}
        </svg>
      </div>
    {/each}
  </div>
{/if}

<style>
  .topology-container {
    margin-top: 4px;
  }

  .gpu-group {
    margin-bottom: 8px;
  }

  .topology-svg {
    display: block;
    border: 1px solid #232433;
    border-radius: 3px;
    background: #16161e;
    overflow: visible;
  }

  :global(html.light) .topology-svg {
    background: #e9e9ec;
    border-color: #c4c5ca;
  }

  :global(html.light) .topology-svg rect {
    fill: #d5d6db;
    stroke: #34548a;
  }

  :global(html.light) .topology-svg text {
    fill: #343b58;
  }
</style>
