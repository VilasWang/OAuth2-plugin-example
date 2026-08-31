<script setup lang="ts">
defineProps<{
  variant?: 'default' | 'success' | 'warning' | 'error' | 'info'
  size?: 'sm' | 'md'
}>()
</script>

<template>
  <span
    :class="[
      'badge inline-flex items-center gap-1.5 font-semibold rounded-full select-none whitespace-nowrap',
      'text-[11.5px] leading-none border',
      size === 'sm' ? 'px-[7px] py-1' : 'px-[9px] py-[5px]',

      variant === 'success'
        ? 'text-(--color-success) bg-(--color-success-bg) border-(--color-success-border)'
        : variant === 'warning'
          ? 'text-(--color-warning) bg-(--color-warning-bg) border-(--color-warning-border)'
          : variant === 'error'
            ? 'text-(--color-error) bg-(--color-error-bg) border-(--color-error-border)'
            : variant === 'info'
              ? 'text-(--color-info) bg-(--color-info-bg) border-(--color-info-border)'
              : 'text-neutral-600 bg-neutral-100 border-neutral-200',
    ]"
  >
    <slot />
  </span>
</template>

<style scoped>
/* Status dot — mockup .pill::before */
.badge::before {
  content: '';
  width: 5px;
  height: 5px;
  border-radius: 50%;
  background: currentColor;
}
</style>
