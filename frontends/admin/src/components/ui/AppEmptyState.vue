<script setup lang="ts">
defineProps<{
  icon?: string
  title: string
  description?: string
  actionLabel?: string
}>()

defineEmits<{
  action: []
}>()
</script>

<template>
  <div class="flex flex-col items-center justify-center py-16 px-6 text-center">
    <!-- Icon slot or default -->
    <div class="w-16 h-16 rounded-2xl bg-neutral-100 flex items-center justify-center mb-5">
      <slot name="icon">
        <svg
          class="w-8 h-8 text-neutral-400"
          viewBox="0 0 24 24"
          fill="none"
          stroke="currentColor"
          stroke-width="1.5"
          stroke-linecap="round"
          aria-hidden="true"
        >
          <path d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4" />
        </svg>
      </slot>
    </div>

    <h3 class="text-base font-semibold text-neutral-900 mb-1.5">
      {{ title }}
    </h3>
    <p
      v-if="description"
      class="text-sm text-neutral-500 max-w-sm mb-6"
    >
      {{ description }}
    </p>

    <button
      v-if="actionLabel"
      type="button"
      class="inline-flex items-center gap-2 px-4 py-2.5 bg-sky-700 text-white text-sm font-medium rounded-lg
             hover:bg-sky-800 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-sky-500 focus-visible:ring-offset-2
             transition-colors shadow-sm"
      @click="$emit('action')"
    >
      <svg
        class="w-4 h-4"
        viewBox="0 0 20 20"
        fill="currentColor"
        aria-hidden="true"
      >
        <path d="M10.75 4.75a.75.75 0 00-1.5 0v4.5h-4.5a.75.75 0 000 1.5h4.5v4.5a.75.75 0 001.5 0v-4.5h4.5a.75.75 0 000-1.5h-4.5v-4.5z" />
      </svg>
      {{ actionLabel }}
    </button>
  </div>
</template>
