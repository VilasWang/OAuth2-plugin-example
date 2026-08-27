<script setup lang="ts">
import { ref } from 'vue'

defineProps<{
  type?: 'info' | 'success' | 'warning' | 'error'
  title?: string
  dismissible?: boolean
}>()

const emit = defineEmits<{
  dismiss: []
}>()

const visible = ref(true)

function handleDismiss() {
  visible.value = false
  emit('dismiss')
}
</script>

<template>
  <div
    v-if="visible"
    :class="[
      'flex items-start gap-3 rounded-lg px-4 py-3 text-sm border transition-all duration-200',
      type === 'success'
        ? 'bg-emerald-50 text-emerald-800 border-emerald-200'
        : type === 'warning'
          ? 'bg-amber-50 text-amber-800 border-amber-200'
          : type === 'error'
            ? 'bg-rose-50 text-rose-800 border-rose-200'
            : 'bg-sky-50 text-sky-800 border-sky-200',
    ]"
    role="alert"
  >
    <svg
      v-if="type === 'success'"
      class="w-5 h-5 shrink-0 mt-0.5"
      viewBox="0 0 20 20"
      fill="currentColor"
      aria-hidden="true"
    >
      <path
        fill-rule="evenodd"
        d="M10 18a8 8 0 100-16 8 8 0 000 16zm3.857-9.809a.75.75 0 00-1.214-.882l-3.483 4.79-1.88-1.88a.75.75 0 10-1.06 1.061l2.5 2.5a.75.75 0 001.137-.089l4-5.5z"
      />
    </svg>
    <svg
      v-else-if="type === 'warning'"
      class="w-5 h-5 shrink-0 mt-0.5"
      viewBox="0 0 20 20"
      fill="currentColor"
      aria-hidden="true"
    >
      <path
        fill-rule="evenodd"
        d="M8.485 2.495c.673-1.167 2.357-1.167 3.03 0l6.28 10.875c.673 1.167-.17 2.625-1.517 2.625H3.72c-1.347 0-2.19-1.458-1.516-2.625L8.485 2.495zM10 6a.75.75 0 01.75.75v3.5a.75.75 0 01-1.5 0v-3.5A.75.75 0 0110 6zm0 9a1 1 0 100-2 1 1 0 000 2z"
      />
    </svg>
    <svg
      v-else-if="type === 'error'"
      class="w-5 h-5 shrink-0 mt-0.5"
      viewBox="0 0 20 20"
      fill="currentColor"
      aria-hidden="true"
    >
      <path
        fill-rule="evenodd"
        d="M10 18a8 8 0 100-16 8 8 0 000 16zM8.28 7.22a.75.75 0 00-1.06 1.06L8.94 10l-1.72 1.72a.75.75 0 101.06 1.06L10 11.06l1.72 1.72a.75.75 0 101.06-1.06L11.06 10l1.72-1.72a.75.75 0 00-1.06-1.06L10 8.94 8.28 7.22z"
      />
    </svg>
    <svg
      v-else
      class="w-5 h-5 shrink-0 mt-0.5"
      viewBox="0 0 20 20"
      fill="currentColor"
      aria-hidden="true"
    >
      <path
        fill-rule="evenodd"
        d="M18 10a8 8 0 11-16 0 8 8 0 0116 0zm-7-4a1 1 0 11-2 0 1 1 0 012 0zM9 9a.75.75 0 000 1.5h.253a.25.25 0 01.244.304l-.459 2.066A1.75 1.75 0 0010.747 15H11a.75.75 0 000-1.5h-.253a.25.25 0 01-.244-.304l.459-2.066A1.75 1.75 0 009.253 9H9z"
      />
    </svg>

    <div class="flex-1 min-w-0">
      <p
        v-if="title"
        class="font-medium"
      >
        {{ title }}
      </p>
      <div :class="title ? 'mt-0.5' : ''">
        <slot />
      </div>
    </div>

    <button
      v-if="dismissible"
      type="button"
      class="shrink-0 p-0.5 rounded hover:opacity-70 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-current"
      aria-label="Dismiss"
      @click="handleDismiss"
    >
      <svg
        class="w-4 h-4"
        viewBox="0 0 16 16"
        fill="currentColor"
        aria-hidden="true"
      >
        <path d="M5.28 4.22a.75.75 0 00-1.06 1.06L6.94 8l-2.72 2.72a.75.75 0 101.06 1.06L8 9.06l2.72 2.72a.75.75 0 101.06-1.06L9.06 8l2.72-2.72a.75.75 0 00-1.06-1.06L8 6.94 5.28 4.22z" />
      </svg>
    </button>
  </div>
</template>
