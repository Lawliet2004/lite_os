# UI Design

## Visual Identity

LiteNix should feel polished and lightweight, but not imitative of macOS. The visual language should use rounded windows, translucent surfaces, and a calm light theme with strong contrast.

## Color Tokens

- `bg-base`: warm off-white
- `bg-surface`: translucent panel fill
- `bg-elevated`: higher-opacity panel fill
- `text-primary`: near-black
- `text-secondary`: muted gray
- `accent`: deep blue or teal
- `danger`: muted red

## Typography

- Use a clean, highly legible sans-serif
- Prefer system or bundled open fonts with clear licensing
- Keep line heights generous and sizes consistent across shell, launcher, and app chrome

## Window Metrics

- Default corner radius: 12 px
- Title bar height: 28 to 32 px
- Dock item size: 40 to 52 px
- Panel padding: 12 to 16 px

## Dock Behavior

- Bottom-aligned dock
- Hover enlargement should be subtle
- Launch icons, running indicators, and separators should be visually distinct

## Top Bar Behavior

- Global status bar across the top
- Time, network, battery, and session controls
- Keep it quiet unless the user interacts

## Animation Budget

- Use short transitions only
- Avoid continuous animation when idle
- Prefer 120 to 180 ms fades and slides

## Accessibility

- Keyboard-only navigation must be possible
- Focus indicators must be obvious
- High-contrast mode should be available
- Text scaling should not break layout

## Performance Budget

- Software rendering first
- Dirty-rectangle updates only
- No unnecessary full-screen redraws
- Target 128 MB boot shell and 256 to 512 MB desktop

## What Is Not Copied From macOS

- Apple icons
- Apple fonts
- Apple menu bar branding
- Apple window chrome shapes as a direct clone
- Apple trade dress
