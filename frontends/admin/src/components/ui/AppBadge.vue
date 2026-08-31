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
        ? 'bg-success-100 text-success-700'
        : variant === 'warning'
          ? 'bg-warning-100 text-warning-700'
          : variant === 'error'
            ? 'bg-error-100 text-error-700'
            : variant === 'info'
              ? 'bg-brand-100 text-brand-700'
              : 'bg-neutral-100 text-neutral-700',
    ]"
  >
    <slot />
  </span>
</template>
