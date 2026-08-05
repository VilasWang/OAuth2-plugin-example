<script setup lang="ts">
defineProps<{
  type?: 'card' | 'table-row' | 'text'
  count?: number
}>()
</script>

<template>
  <!-- Card skeleton -->
  <template v-if="type === 'card' || !type">
    <div class="animate-pulse space-y-4">
      <div v-for="i in (count || 3)" :key="i" class="bg-white rounded-xl border border-neutral-200 p-6">
        <div class="flex items-center gap-3 mb-4">
          <div class="w-10 h-10 rounded-full bg-neutral-100" />
          <div class="space-y-2 flex-1">
            <div class="h-3.5 bg-neutral-100 rounded w-1/3" />
            <div class="h-3 bg-neutral-100 rounded w-1/4" />
          </div>
        </div>
        <div class="space-y-2">
          <div class="h-3 bg-neutral-100 rounded w-full" />
          <div class="h-3 bg-neutral-100 rounded w-2/3" />
        </div>
      </div>
    </div>
  </template>

  <!-- Table row skeleton -->
  <template v-else-if="type === 'table-row'">
    <div v-for="i in (count || 5)" :key="i" class="animate-pulse flex items-center gap-4 px-4 py-3">
      <div class="h-4 bg-neutral-100 rounded flex-1" />
      <div class="h-4 bg-neutral-100 rounded w-1/4" />
      <div class="h-4 bg-neutral-100 rounded w-1/6" />
    </div>
  </template>

  <!-- Text skeleton -->
  <template v-else>
    <div class="animate-pulse space-y-2">
      <div v-for="i in (count || 3)" :key="i" class="h-4 bg-neutral-100 rounded" :style="{ width: (60 + Math.random() * 35) + '%' }" />
    </div>
  </template>
</template>
