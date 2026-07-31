<script setup lang="ts">
defineProps<{ size?: 'sm' | 'md' | 'lg' }>()
</script>

<template>
  <div class="flex items-center gap-2" :class="size === 'lg' ? 'gap-3' : ''">
    <div class="rounded-xl bg-indigo-600 flex items-center justify-center text-white font-bold"
      :class="size === 'lg' ? 'w-10 h-10 text-lg' : size === 'sm' ? 'w-7 h-7 text-xs' : 'w-8 h-8 text-sm'">
      O2
    </div>
    <span class="font-semibold text-gray-900" :class="size === 'lg' ? 'text-xl' : size === 'sm' ? 'text-sm' : 'text-base'">
      {{ $attrs.title || 'OAuth2' }}
    </span>
  </div>
</template>
