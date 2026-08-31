<script setup lang="ts">
defineProps<{
  padding?: 'none' | 'sm' | 'md' | 'lg'
  hover?: boolean
}>()
</script>

<template>
  <div
    :class="[
      'bg-surface border border-neutral-200 rounded-xl overflow-hidden transition-all duration-150',
      hover ? 'hover:border-neutral-300 hover:shadow-md' : 'shadow-sm',
      padding === 'none' ? '' :
      padding === 'sm' ? 'p-4' :
      padding === 'lg' ? 'p-8' :
      'p-6',
    ]"
  >
    <div
      v-if="$slots.header"
      class="flex items-center justify-between mb-5"
    >
      <slot name="header" />
    </div>
    <slot />
  </div>
</template>
