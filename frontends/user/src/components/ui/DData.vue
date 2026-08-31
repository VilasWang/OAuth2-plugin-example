<script setup lang="ts">
withDefaults(defineProps<{
  value: string
  label?: string
  truncate?: boolean
  mono?: boolean
}>(), {
  mono: true,
})
</script>

<template>
  <span
    :class="[
      'inline-flex items-center gap-1.5 whitespace-nowrap',
      'text-[11.5px] leading-none px-2 py-[5px] rounded-ctl',
      'bg-neutral-100 border border-neutral-200 text-neutral-600',
      mono ? 'font-mono tabular-nums' : '',
      truncate ? 'max-w-full min-w-0' : '',
    ]"
  >
    <span
      v-if="label"
      class="text-neutral-400"
    >{{ label }}</span>
    <span :class="truncate ? 'truncate' : ''">{{ value }}</span>
  </span>
</template>
