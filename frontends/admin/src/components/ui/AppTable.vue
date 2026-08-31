<script setup lang="ts" generic="T extends Record<string, any>">

defineProps<{
  columns: { key: string; label: string; sortable?: boolean; align?: 'left' | 'center' | 'right'; width?: string }[]
  rows: T[]
  rowKey?: string
  loading?: boolean
  emptyText?: string
  emptyIcon?: boolean
}>()

defineEmits<{
  sort: [key: string]
}>()

const colAlign = (col: { align?: string }) => {
  if (col.align === 'center') return 'text-center'
  if (col.align === 'right') return 'text-right'
  return 'text-left'
}
</script>

<template>
  <div class="overflow-hidden rounded-xl border border-neutral-200 bg-surface">
    <div class="overflow-x-auto">
      <table class="min-w-full divide-y divide-neutral-200">
        <thead>
          <tr class="bg-neutral-50">
            <th
              v-for="col in columns"
              :key="col.key"
              :style="col.width ? { width: col.width } : {}"
              :class="[
                'px-4 py-3 text-xs font-medium text-neutral-500 uppercase tracking-wider select-none',
                colAlign(col),
              ]"
            >
              <button
                v-if="col.sortable"
                class="inline-flex items-center gap-1 hover:text-neutral-700 transition-colors"
                @click="$emit('sort', col.key)"
              >
                {{ col.label }}
                <svg
                  class="w-3 h-3"
                  viewBox="0 0 12 12"
                  fill="currentColor"
                  aria-hidden="true"
                >
                  <path d="M6 1L9 5H3L6 1zM6 11L3 7h6L6 11z" />
                </svg>
              </button>
              <span v-else>{{ col.label }}</span>
            </th>
          </tr>
        </thead>

        <tbody class="divide-y divide-neutral-100">
          <!-- Loading skeleton -->
          <template v-if="loading">
            <tr
              v-for="i in 5"
              :key="i"
              class="animate-pulse"
            >
              <td
                v-for="col in columns"
                :key="col.key"
                class="px-4 py-3"
              >
                <div
                  class="h-4 bg-neutral-100 rounded"
                  :style="{ width: (60 + Math.random() * 30) + '%' }"
                />
              </td>
            </tr>
          </template>

          <!-- Empty state -->
          <tr v-else-if="rows.length === 0">
            <td
              :colspan="columns.length"
              class="px-4 py-16 text-center"
            >
              <svg
                v-if="emptyIcon !== false"
                class="w-10 h-10 text-neutral-300 mx-auto mb-3"
                viewBox="0 0 20 20"
                fill="currentColor"
                aria-hidden="true"
              >
                <path
                  fill-rule="evenodd"
                  d="M2 4.75A.75.75 0 012.75 4h14.5a.75.75 0 010 1.5H2.75A.75.75 0 012 4.75zM2 10a.75.75 0 01.75-.75h14.5a.75.75 0 010 1.5H2.75A.75.75 0 012 10zm0 5.25a.75.75 0 01.75-.75h14.5a.75.75 0 010 1.5H2.75a.75.75 0 01-.75-.75z"
                />
              </svg>
              <p class="text-sm text-neutral-500">
                {{ emptyText || 'No data found' }}
              </p>
            </td>
          </tr>

          <!-- Data rows -->
          <tr
            v-for="(row, idx) in rows"
            v-else
            :key="rowKey ? row[rowKey] : idx"
            class="hover:bg-neutral-50/80 transition-colors"
          >
            <td
              v-for="col in columns"
              :key="col.key"
              :class="['px-4 py-3 text-sm text-neutral-700', colAlign(col)]"
            >
              <slot
                :name="'cell-' + col.key"
                :row="row"
                :value="row[col.key]"
                :index="idx"
              >
                {{ row[col.key] }}
              </slot>
            </td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- Footer -->
    <div
      v-if="$slots.footer"
      class="px-4 py-3 border-t border-neutral-100 bg-neutral-50"
    >
      <slot name="footer" />
    </div>
  </div>
</template>
