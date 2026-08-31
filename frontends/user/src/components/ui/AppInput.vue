<script setup lang="ts">
import { useId } from 'vue'

defineProps<{
  modelValue?: string
  label?: string
  type?: string
  placeholder?: string
  error?: string
  hint?: string
  required?: boolean
  disabled?: boolean
  autocomplete?: string
}>()

defineEmits<{
  'update:modelValue': [value: string]
}>()

const inputId = useId()
const errorId = useId()
</script>

<template>
  <div class="space-y-1.5">
    <label
      v-if="label"
      :for="inputId"
      class="block text-sm font-medium text-neutral-700 select-none"
    >
      {{ label }}
      <span
        v-if="required"
        class="text-error-500 ml-0.5"
        aria-hidden="true"
      >*</span>
    </label>

    <div class="relative">
      <input
        :id="inputId"
        :type="type || 'text'"
        :value="modelValue"
        :placeholder="placeholder"
        :required="required"
        :disabled="disabled"
        :autocomplete="autocomplete"
        :aria-invalid="!!error"
        :aria-describedby="error ? errorId : undefined"
        class="block w-full px-3.5 py-2.5 text-sm rounded-lg transition-colors duration-150
               bg-surface text-neutral-900
               placeholder:text-neutral-400
               disabled:bg-neutral-50 disabled:text-neutral-500 disabled:cursor-not-allowed
               focus:outline-none focus:ring-2 focus:ring-offset-0"
        :class="error
          ? 'border border-error-200 focus:ring-error-500/20 focus:border-error-500'
          : 'border border-neutral-300 focus:ring-brand-500/20 focus:border-brand-700'"
        @input="$emit('update:modelValue', ($event.target as HTMLInputElement).value)"
      >

      <div
        v-if="error"
        class="absolute inset-y-0 right-0 flex items-center pr-3 pointer-events-none"
      >
        <svg
          class="w-4 h-4 text-error-500"
          viewBox="0 0 16 16"
          fill="currentColor"
          aria-hidden="true"
        >
          <path
            fill-rule="evenodd"
            d="M8 1a7 7 0 100 14A7 7 0 008 1zM7.25 4.5a.75.75 0 011.5 0v3a.75.75 0 01-1.5 0v-3zm.75 6.25a.75.75 0 100-1.5.75.75 0 000 1.5z"
          />
        </svg>
      </div>
    </div>

    <p
      v-if="error"
      :id="errorId"
      class="text-xs text-error-600"
      role="alert"
    >
      {{ error }}
    </p>
    <p
      v-else-if="hint"
      class="text-xs text-neutral-500"
    >
      {{ hint }}
    </p>
  </div>
</template>
