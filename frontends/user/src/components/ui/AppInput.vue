<script setup lang="ts">
defineProps<{
  modelValue?: string
  label?: string
  type?: string
  placeholder?: string
  error?: string
  required?: boolean
  autocomplete?: string
  disabled?: boolean
}>()

defineEmits<{ 'update:modelValue': [value: string] }>()
</script>

<template>
  <div>
    <label v-if="label" class="block text-sm font-medium text-gray-700 mb-1.5">
      {{ label }}
      <span v-if="required" class="text-rose-500">*</span>
    </label>
    <input
      :type="type || 'text'"
      :value="modelValue"
      @input="$emit('update:modelValue', ($event.target as HTMLInputElement).value)"
      :placeholder="placeholder"
      :required="required"
      :autocomplete="autocomplete"
      :disabled="disabled"
      class="block w-full px-4 py-2.5 text-gray-900 bg-white border rounded-lg text-sm transition-colors duration-200 placeholder:text-gray-400 focus:outline-none focus:ring-2 focus:ring-indigo-500/20 focus:border-indigo-500 disabled:bg-gray-50 disabled:text-gray-500"
      :class="error ? 'border-rose-300 focus:ring-rose-500/20 focus:border-rose-500' : 'border-gray-300'"
    />
    <p v-if="error" class="mt-1.5 text-sm text-rose-600">{{ error }}</p>
  </div>
</template>
