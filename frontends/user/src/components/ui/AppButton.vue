<script setup lang="ts">
defineProps<{
  variant?: 'primary' | 'secondary' | 'danger' | 'ghost'
  size?: 'sm' | 'md' | 'lg'
  loading?: boolean
  disabled?: boolean
  type?: 'button' | 'submit' | 'reset'
  block?: boolean
}>()

defineEmits<{
  click: [e: MouseEvent]
}>()
</script>

<template>
  <button
    :type="type || 'button'"
    :disabled="disabled || loading"
    :class="[
      'inline-flex items-center justify-center gap-2 font-medium',
      'rounded-lg transition-all duration-150 select-none',
      'focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-offset-2',
      'disabled:opacity-50 disabled:cursor-not-allowed',
      'active:scale-[0.98]',
      block ? 'w-full' : '',

      size === 'sm' ? 'px-3 py-1.5 text-xs leading-4 gap-1.5' :
      size === 'lg' ? 'px-6 py-3 text-base leading-6 gap-2.5' :
      'px-4 py-2.5 text-sm leading-5',

      variant === 'secondary'
        ? 'bg-white text-neutral-700 border border-neutral-300 hover:bg-neutral-50 hover:border-neutral-400 focus-visible:ring-neutral-300 shadow-sm'
        : variant === 'danger'
          ? 'bg-rose-600 text-white hover:bg-rose-700 focus-visible:ring-rose-500 shadow-sm'
          : variant === 'ghost'
            ? 'text-neutral-600 hover:text-neutral-900 hover:bg-neutral-100 focus-visible:ring-neutral-300'
            : 'bg-sky-700 text-white hover:bg-sky-800 focus-visible:ring-sky-500 shadow-sm',
    ]"
    @click="$emit('click', $event)"
  >
    <svg
      v-if="loading"
      class="animate-spin shrink-0"
      :class="size === 'sm' ? 'w-3.5 h-3.5' : 'w-4 h-4'"
      viewBox="0 0 24 24"
      fill="none"
      aria-hidden="true"
    >
      <circle
        class="opacity-25"
        cx="12"
        cy="12"
        r="10"
        stroke="currentColor"
        stroke-width="4"
      />
      <path
        class="opacity-75"
        fill="currentColor"
        d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z"
      />
    </svg>
    <slot />
  </button>
</template>
