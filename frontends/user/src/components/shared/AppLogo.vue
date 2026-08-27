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
    <div
      :class="[
        'rounded-lg bg-sky-700 flex items-center justify-center text-white font-bold tracking-tight',
        size === 'lg' ? 'w-10 h-10 text-lg' :
        size === 'sm' ? 'w-7 h-7 text-xs' :
        'w-8 h-8 text-sm',
      ]"
    >
      AF
    </div>
    <span
      :class="[
        'font-semibold text-neutral-900 tracking-tight',
        size === 'lg' ? 'text-xl' :
        size === 'sm' ? 'text-sm' :
        'text-base',
      ]"
    >
      Fulla
    </span>
  </div>
</template>
