#ifndef PMM_H
#define PMM_H

/*
 * Copyright 2021 NSG650
 * Copyright 2021 Sebastian
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stddef.h>
#include <stivale2.h>

void *pmm_alloc(size_t count);
void *pmm_allocz(size_t count);
void pmm_free(void *ptr, size_t count);
void pmm_init(struct stivale2_mmap_entry *memmap, size_t memmap_entries);

#endif
