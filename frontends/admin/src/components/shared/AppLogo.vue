<script setup lang="ts">
defineProps<{
  size?: 'sm' | 'md' | 'lg'
  monochrome?: boolean
}>()
</script>

<template>
  <div class="flex items-center gap-2.5 select-none" :class="size === 'lg' ? 'gap-3' : ''">
    <!-- Logo mark -->
    <div
      :class="[
        'rounded-lg flex items-center justify-center font-bold tracking-tight',
        size === 'lg' ? 'w-10 h-10 text-lg' :
        size === 'sm' ? 'w-7 h-7 text-xs' :
        'w-8 h-8 text-sm',
        monochrome
          ? 'bg-neutral-800 text-white'
          : 'bg-sky-700 text-white',
      ]"
    >
      AF
    </div>

    <!-- Logo text -->
    <div>
      <span
        :class="[
          'font-semibold tracking-tight',
          size === 'lg' ? 'text-xl' :
          size === 'sm' ? 'text-sm' :
          'text-base',
          monochrome ? 'text-neutral-900' : 'text-neutral-900',
        ]"
      >
        Fulla
      </span>
      <span
        v-if="size === 'lg'"
        class="block text-xs text-neutral-400 font-normal tracking-normal mt-0.5"
      >
        Identity Platform
      </span>
    </div>
  </div>
</template>
