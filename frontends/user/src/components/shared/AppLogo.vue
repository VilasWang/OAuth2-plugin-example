<script setup lang="ts">
defineProps<{
  size?: 'sm' | 'md' | 'lg'
}>()
</script>

<template>
  <div
    class="flex items-center gap-2.5 select-none"
    :class="size === 'lg' ? 'gap-3' : ''"
  >
    <!-- Logo mark: keyhole tile -->
    <svg
      viewBox="0 0 40 40"
      aria-hidden="true"
      :class="
        size === 'lg' ? 'w-10 h-10' :
        size === 'sm' ? 'w-7 h-7' :
        'w-8 h-8'
      "
    >
      <rect
        width="40"
        height="40"
        rx="12.5"
        fill="var(--color-brand-600)"
      />
      <circle
        cx="20"
        cy="15.4"
        r="6.1"
        fill="#fff"
      />
      <path
        d="M17.6 19.9 14.9 29.3a.95.95 0 0 0 .92 1.2h8.36a.95.95 0 0 0 .92-1.2l-2.7-9.4Z"
        fill="#fff"
      />
    </svg>

    <span
      :class="[
        'font-display font-semibold text-neutral-900 tracking-tight',
        size === 'lg' ? 'text-xl' :
        size === 'sm' ? 'text-sm' :
        'text-base',
      ]"
    >
      Fulla
    </span>
  </div>
</template>
