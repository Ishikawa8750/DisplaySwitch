<!--
  TopologyView — Phase 7 (Adaptive)
  Visual display topology: GPU → connection → display.
  Pure SVG, fully responsive. Auto-scales to container width.
-->
<script lang="ts">
  import type { DisplayInfo } from "./types";

  let { displays }: { displays: DisplayInfo[] } = $props();

  // Container width for responsive layout
  let containerWidth = $state(600);

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

  function connColor(type: string): string {
    const t = type.toLowerCase();
    if (t.includes("thunderbolt") || t.includes("usb-c") || t.includes("usb4")) return "#e0af68";
    if (t.includes("dp") || t.includes("displayport")) return "#9ece6a";
    if (t.includes("hdmi")) return "#7aa2f7";
    if (t.includes("edp") || t.includes("lvds")) return "#bb9af7";
    return "#565a89";
  }

  function connLabel(d: DisplayInfo): string {
    return d.hdmi_version ?? d.connection_type;
  }

  function bwLabel(d: DisplayInfo): string {
    return d.bandwidth?.bandwidth_str ?? "";
  }

  // Adaptive layout calculations — all derived from containerWidth
  const MARGIN = 20;
  const NODE_GAP_Y = 16;
  const MIN_GAP_X = 80;

  let layout = $derived.by(() => {
    const w = containerWidth - MARGIN * 2;
    // Node widths scale with container, clamped to reasonable bounds
    const gpuW = Math.max(100, Math.min(160, w * 0.2));
    const gpuH = 44;
    const dispW = Math.max(140, Math.min(220, w * 0.32));
    const dispH = 54;
    // Gap between GPU and display column
    const gapX = Math.max(MIN_GAP_X, w - gpuW - dispW);
    // Display column X
    const dispX = MARGIN + gpuW + gapX;
    // Font sizes scale slightly
    const baseFontSize = Math.max(8, Math.min(11, w / 55));

    return { gpuW, gpuH, dispW, dispH, gapX, dispX, baseFontSize };
  });

  // Truncate text to fit a given pixel width (approximate)
  function truncate(text: string, maxChars: number): string {
    if (text.length <= maxChars) return text;
    return text.slice(0, maxChars) + "…";
  }

  let maxGpuNameChars = $derived(Math.max(10, Math.floor(layout.gpuW / 7)));
  let maxDispNameChars = $derived(Math.max(10, Math.floor(layout.dispW / 7)));
</script>

{#if displays.length > 0}
  <div class="topology-container" bind:clientWidth={containerWidth}>
    {#each [...gpuGroups] as [gpuName, gpuDisplays]}
      {@const { gpuW, gpuH, dispW, dispH, dispX, baseFontSize } = layout}
      {@const groupH = MARGIN * 2 + gpuDisplays.length * (dispH + NODE_GAP_Y) - NODE_GAP_Y}
      {@const gpuX = MARGIN}
      {@const gpuY = MARGIN + ((gpuDisplays.length * (dispH + NODE_GAP_Y) - NODE_GAP_Y) / 2) - gpuH / 2}

      <svg
        width="100%"
        viewBox={`0 0 ${containerWidth} ${groupH}`}
        preserveAspectRatio="xMidYMid meet"
        class="topology-svg"
      >
        <!-- GPU Node -->
        <rect
          x={gpuX} y={gpuY}
          width={gpuW} height={gpuH}
          rx="4" class="node-gpu"
        />
        <text x={gpuX + gpuW / 2} y={gpuY + 17}
          text-anchor="middle" class="text-title" font-size={baseFontSize + 1}>
          GPU
        </text>
        <text x={gpuX + gpuW / 2} y={gpuY + 33}
          text-anchor="middle" class="text-sub" font-size={baseFontSize - 1}>
          {truncate(gpuName, maxGpuNameChars)}
        </text>

        <!-- Display Nodes + Connections -->
        {#each gpuDisplays as disp, di}
          {@const dy = MARGIN + di * (dispH + NODE_GAP_Y)}
          {@const color = connColor(disp.connection_type)}
          {@const x1 = gpuX + gpuW}
          {@const y1 = gpuY + gpuH / 2}
          {@const x2 = dispX}
          {@const y2 = dy + dispH / 2}
          {@const midX = (x1 + x2) / 2}
          {@const midY = (y1 + y2) / 2}

          <!-- Bezier connection -->
          <path
            d={`M${x1},${y1} C${x1 + (x2 - x1) * 0.4},${y1} ${x2 - (x2 - x1) * 0.4},${y2} ${x2},${y2}`}
            fill="none" stroke={color} stroke-width="1.5"
            stroke-dasharray={disp.is_internal ? "none" : "6,3"}
            class="conn-line"
          />

          <!-- Connection badge -->
          <rect
            x={midX - 36} y={midY - (bwLabel(disp) ? 14 : 9)}
            width="72" height={bwLabel(disp) ? 28 : 18}
            rx="9" fill="var(--badge-bg)" fill-opacity="0.92"
          />
          <text x={midX} y={midY + (bwLabel(disp) ? -3 : 4)}
            text-anchor="middle" fill={color}
            font-size={baseFontSize - 1} font-weight="500">
            {connLabel(disp)}
          </text>
          {#if bwLabel(disp)}
            <text x={midX} y={midY + 10}
              text-anchor="middle" class="text-sub" font-size={baseFontSize - 2}>
              {bwLabel(disp)}
            </text>
          {/if}

          <!-- Display node -->
          <rect
            x={dispX} y={dy}
            width={dispW} height={dispH}
            rx="4" class="node-display" stroke={color} stroke-width="1"
          />
          <!-- HDR / internal indicator dot -->
          {#if disp.supports_hdr}
            <circle cx={dispX + dispW - 10} cy={dy + 12} r="3.5" fill="#e0af68" opacity="0.8" />
          {/if}

          <text x={dispX + 10} y={dy + 17}
            class="text-title" font-size={baseFontSize} font-weight="500">
            {disp.is_internal ? "● " : ""}{truncate(disp.name, maxDispNameChars)}
          </text>
          <text x={dispX + 10} y={dy + 31}
            class="text-body" font-size={baseFontSize - 1}>
            {disp.resolution_str} · {disp.refresh_rate.toFixed(0)}Hz
          </text>
          <text x={dispX + 10} y={dy + 44}
            class="text-sub" font-size={baseFontSize - 2}>
            {disp.manufacturer_id} {disp.product_code}{#if disp.brightness >= 0} · {disp.brightness}%{/if}
          </text>
        {/each}
      </svg>
    {/each}
  </div>
{/if}

<style>
  .topology-container {
    width: 100%;
    display: flex;
    flex-direction: column;
    gap: 8px;
    animation: topo-fade-in 0.4s ease-out;
  }

  @keyframes topo-fade-in {
    from { opacity: 0; transform: translateY(10px); }
    to   { opacity: 1; transform: translateY(0); }
  }

  .topology-svg {
    display: block;
    border: 1px solid #232433;
    border-radius: 6px;
    background: #16161e;
    --badge-bg: #1a1b26;
  }

  :global(html.light) .topology-svg {
    background: #e9e9ec;
    border-color: #c4c5ca;
    --badge-bg: #e9e9ec;
  }

  /* GPU node */
  .node-gpu {
    fill: #24283b;
    stroke: #7aa2f7;
    stroke-width: 1;
    transition: filter 0.2s;
  }
  .node-gpu:hover {
    filter: drop-shadow(0 0 6px rgba(122,162,247,0.4));
  }
  :global(html.light) .node-gpu {
    fill: #d5d6db;
    stroke: #34548a;
  }

  /* Display node */
  .node-display {
    fill: #1a1b26;
    transition: filter 0.2s, transform 0.2s;
  }
  .node-display:hover {
    filter: drop-shadow(0 0 6px rgba(122,162,247,0.3));
  }
  :global(html.light) .node-display {
    fill: #d5d6db;
  }

  /* Text classes */
  .text-title { fill: #c0caf5; }
  .text-body  { fill: #a9b1d6; }
  .text-sub   { fill: #565a89; }

  :global(html.light) .text-title { fill: #343b58; }
  :global(html.light) .text-body  { fill: #4c505e; }
  :global(html.light) .text-sub   { fill: #8990b3; }

  /* Connection hover */
  .conn-line {
    transition: stroke-width 0.15s;
    animation: dash-flow 1.5s linear infinite;
  }
  @keyframes dash-flow {
    to { stroke-dashoffset: -18; }
  }
  .conn-line:hover {
    stroke-width: 2.5;
  }
</style>
