<script setup lang="ts">
defineProps<{
  variant?: 'primary' | 'secondary' | 'danger' | 'ghost'
  size?: 'sm' | 'md' | 'lg'
  loading?: boolean
  disabled?: boolean
  type?: 'button' | 'submit'
}>()
</script>

<template>
  <button
    :type="type || 'button'"
    :disabled="disabled || loading"
    class="inline-flex items-center justify-center font-medium rounded-lg transition-all duration-200 focus:outline-none focus:ring-2 focus:ring-offset-2 disabled:opacity-50 disabled:cursor-not-allowed"
    :class="[
      variant === 'secondary' ? 'bg-white text-gray-700 border border-gray-300 hover:bg-gray-50 focus:ring-gray-300' :
      variant === 'danger' ? 'bg-rose-600 text-white hover:bg-rose-700 focus:ring-rose-500' :
      variant === 'ghost' ? 'text-gray-600 hover:text-gray-900 hover:bg-gray-100 focus:ring-gray-300' :
      'bg-indigo-600 text-white hover:bg-indigo-700 focus:ring-indigo-500 shadow-sm',
      size === 'sm' ? 'px-3 py-1.5 text-sm' :
      size === 'lg' ? 'px-6 py-3 text-base' :
      'px-4 py-2.5 text-sm',
    ]"
  >
    <svg v-if="loading" class="animate-spin -ml-1 mr-2 h-4 w-4" fill="none" viewBox="0 0 24 24">
      <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4" />
      <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
    </svg>
    <slot />
  </button>
</template>
