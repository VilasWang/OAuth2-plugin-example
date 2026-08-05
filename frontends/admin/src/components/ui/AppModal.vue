<script setup lang="ts">
import { watch, ref, onUnmounted, nextTick } from 'vue'

const props = defineProps<{
  open: boolean
  title?: string
  size?: 'sm' | 'md' | 'lg'
}>()

const emit = defineEmits<{
  close: []
}>()

const modalRef = ref<HTMLElement | null>(null)
const previousActiveElement = ref<HTMLElement | null>(null)

function handleKeydown(e: KeyboardEvent) {
  if (e.key === 'Escape') {
    emit('close')
  }
  // Trap focus
  if (e.key === 'Tab' && modalRef.value) {
    const focusable = modalRef.value.querySelectorAll<HTMLElement>(
      'button, [href], input, select, textarea, [tabindex]:not([tabindex="-1"])'
    )
    const first = focusable[0]
    const last = focusable[focusable.length - 1]
    if (e.shiftKey && document.activeElement === first) {
      e.preventDefault()
      last?.focus()
    } else if (!e.shiftKey && document.activeElement === last) {
      e.preventDefault()
      first?.focus()
    }
  }
}

watch(() => props.open, async (isOpen) => {
  if (isOpen) {
    previousActiveElement.value = document.activeElement as HTMLElement
    document.body.style.overflow = 'hidden'
    await nextTick()
    modalRef.value?.focus()
  } else {
    document.body.style.overflow = ''
    previousActiveElement.value?.focus()
  }
})

onUnmounted(() => {
  document.body.style.overflow = ''
})
</script>

<template>
  <Teleport to="body">
    <Transition name="modal">
      <div
        v-if="open"
        class="fixed inset-0 z-[400] flex items-center justify-center p-4 sm:p-6"
        role="dialog"
        aria-modal="true"
        :aria-label="title"
        @click.self="emit('close')"
        @keydown="handleKeydown"
      >
        <!-- Backdrop -->
        <div class="fixed inset-0 bg-neutral-900/50 backdrop-blur-sm" aria-hidden="true" />

        <!-- Panel -->
        <div
          ref="modalRef"
          tabindex="-1"
          :class="[
            'relative w-full bg-white rounded-xl shadow-xl border border-neutral-200',
            'max-h-[85vh] overflow-y-auto',
            'outline-none',
            size === 'sm' ? 'max-w-sm' :
            size === 'lg' ? 'max-w-2xl' :
            'max-w-lg',
          ]"
        >
          <!-- Header -->
          <div v-if="title || $slots.header" class="flex items-center justify-between px-6 py-4 border-b border-neutral-100">
            <h2 v-if="title" class="text-lg font-semibold text-neutral-900">{{ title }}</h2>
            <slot name="header" />

            <button
              type="button"
              class="p-1.5 rounded-lg text-neutral-400 hover:text-neutral-600 hover:bg-neutral-100
                     focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-sky-500"
              @click="emit('close')"
              aria-label="Close dialog"
            >
              <svg class="w-5 h-5" viewBox="0 0 20 20" fill="currentColor" aria-hidden="true">
                <path d="M6.28 5.22a.75.75 0 00-1.06 1.06L8.94 10l-3.72 3.72a.75.75 0 101.06 1.06L10 11.06l3.72 3.72a.75.75 0 101.06-1.06L11.06 10l3.72-3.72a.75.75 0 00-1.06-1.06L10 8.94 6.28 5.22z"/>
              </svg>
            </button>
          </div>

          <!-- Body -->
          <div :class="['px-6 py-5', $slots.footer ? '' : 'pb-6']">
            <slot />
          </div>

          <!-- Footer -->
          <div v-if="$slots.footer" class="flex items-center justify-end gap-3 px-6 py-4 border-t border-neutral-100 bg-neutral-50 rounded-b-xl">
            <slot name="footer" />
          </div>
        </div>
      </div>
    </Transition>
  </Teleport>
</template>

<style scoped>
.modal-enter-active { transition: all 200ms ease-out; }
.modal-leave-active { transition: all 150ms ease-in; }
.modal-enter-from { opacity: 0; }
.modal-enter-from > div:last-child { transform: scale(0.95); opacity: 0; }
.modal-leave-to { opacity: 0; }
.modal-leave-to > div:last-child { transform: scale(0.95); opacity: 0; }
</style>
