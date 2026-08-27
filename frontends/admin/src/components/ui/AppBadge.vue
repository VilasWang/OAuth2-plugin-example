<script setup lang="ts">
defineProps<{
  variant?: 'default' | 'success' | 'warning' | 'error' | 'info'
  size?: 'sm' | 'md'
}>()
</script>

<template>
  <span
    :class="[
      'inline-flex items-center font-medium rounded-full select-none',
      size === 'sm' ? 'px-2 py-0.5 text-xs' : 'px-2.5 py-0.5 text-sm',

      variant === 'success'
        ? 'bg-emerald-100 text-emerald-700'
        : variant === 'warning'
          ? 'bg-amber-100 text-amber-700'
          : variant === 'error'
            ? 'bg-rose-100 text-rose-700'
            : variant === 'info'
              ? 'bg-sky-100 text-sky-700'
              : 'bg-neutral-100 text-neutral-700',
    ]"
  >
    <slot />
  </span>
</template>
