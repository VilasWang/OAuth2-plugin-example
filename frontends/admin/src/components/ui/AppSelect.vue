<script setup lang="ts">
import { useId } from 'vue'

defineProps<{
  modelValue?: string
  label?: string
  options: { value: string; label: string; disabled?: boolean }[]
  placeholder?: string
  error?: string
  required?: boolean
  disabled?: boolean
}>()

defineEmits<{
  'update:modelValue': [value: string]
}>()

const selectId = useId()
const errorId = useId()
</script>

<template>
  <div class="space-y-1.5">
    <label v-if="label" :for="selectId" class="block text-sm font-medium text-neutral-700">
      {{ label }}
      <span v-if="required" class="text-rose-500 ml-0.5" aria-hidden="true">*</span>
    </label>

    <select
      :id="selectId"
      :value="modelValue"
      :disabled="disabled"
      :required="required"
      :aria-invalid="!!error"
      :aria-describedby="error ? errorId : undefined"
      :class="[
        'block w-full px-3.5 py-2.5 text-sm rounded-lg transition-colors duration-150',
        'bg-white text-neutral-900 appearance-none cursor-pointer',
        'disabled:bg-neutral-50 disabled:text-neutral-500 disabled:cursor-not-allowed',
        'focus:outline-none focus:ring-2 focus:ring-offset-0',
        error
          ? 'border border-rose-300 focus:ring-rose-500/20 focus:border-rose-500'
          : 'border border-neutral-300 focus:ring-sky-500/20 focus:border-sky-600',
      ]"
      @change="$emit('update:modelValue', ($event.target as HTMLSelectElement).value)"
    >
      <option v-if="placeholder" value="" disabled>{{ placeholder }}</option>
      <option
        v-for="opt in options"
        :key="opt.value"
        :value="opt.value"
        :disabled="opt.disabled"
      >
        {{ opt.label }}
      </option>
    </select>

    <p v-if="error" :id="errorId" class="text-xs text-rose-600" role="alert">{{ error }}</p>
  </div>
</template>
