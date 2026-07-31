<script setup lang="ts">
defineProps<{
  type?: 'success' | 'error' | 'warning' | 'info'
}>()
</script>

<template>
  <div class="rounded-lg px-4 py-3 text-sm flex items-start gap-3" :class="[
    type === 'error' ? 'bg-rose-50 text-rose-700 border border-rose-200' :
    type === 'success' ? 'bg-emerald-50 text-emerald-700 border border-emerald-200' :
    type === 'warning' ? 'bg-amber-50 text-amber-700 border border-amber-200' :
    'bg-blue-50 text-blue-700 border border-blue-200'
  ]">
    <span class="shrink-0 mt-0.5">
      <template v-if="type === 'error'">⚠️</template>
      <template v-else-if="type === 'success'">✓</template>
      <template v-else-if="type === 'warning'">⚡</template>
      <template v-else>ℹ️</template>
    </span>
    <div class="flex-1"><slot /></div>
  </div>
</template>
