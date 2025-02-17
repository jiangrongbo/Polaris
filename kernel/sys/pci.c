/*
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

#include "pci.h"
#include "../cpu/ports.h"
#include "../klibc/printf.h"
#include "../mm/vmm.h"
#include "mmio.h"

mcfg_vec_t mcfg_entries;

static uint32_t (*internal_read)(uint16_t, uint8_t, uint8_t, uint8_t, uint16_t,
								 uint8_t);
static void (*internal_write)(uint16_t, uint8_t, uint8_t, uint8_t, uint16_t,
							  uint32_t, uint8_t);

static uint32_t make_pci_address(uint32_t bus, uint32_t slot, uint32_t function,
								 uint16_t offset) {
	return ((bus << 16) | (slot << 11) | (function << 8) | (offset & 0xFFFC) |
			(1u << 31));
}

static uint32_t legacy_pci_read(uint16_t seg, uint8_t bus, uint8_t slot,
								uint8_t function, uint16_t offset,
								uint8_t access_size) {
	(void)seg;
	port_dword_out(0xCF8, make_pci_address(bus, slot, function, offset));
	switch (access_size) {
		case 1:
			return port_byte_in(0xCFC + (offset & 3));
		case 2:
			return port_word_in(0xCFC + (offset & 2));
		case 4:
			return port_dword_in(0xCFC);
		default:
			printf("PCI: Unknown access size: %hhu\n", access_size);
			return 0;
	}
}

static void legacy_pci_write(uint16_t seg, uint8_t bus, uint8_t slot,
							 uint8_t function, uint16_t offset, uint32_t value,
							 uint8_t access_size) {
	(void)seg;
	port_dword_out(0xCF8, make_pci_address(bus, slot, function, offset));
	switch (access_size) {
		case 1:
			port_byte_out(0xCFC + (offset & 3), value);
			break;
		case 2:
			port_word_out(0xCFC + (offset & 2), value);
			break;
		case 4:
			port_dword_out(0xCFC, value);
			break;
		default:
			printf("PCI: Unknown access size: %hhu\n", access_size);
			break;
	}
}

static uint32_t mcfg_pci_read(uint16_t seg, uint8_t bus, uint8_t slot,
							  uint8_t function, uint16_t offset,
							  uint8_t access_size) {
	for (int i = 0; i < mcfg_entries.length; i++) {
		struct mcfg_entry *entry = mcfg_entries.data[i];
		if (entry->seg == seg) {
			if (bus >= entry->start_bus_number &&
				bus <= entry->end_bus_number) {
				void *addr =
					(void *)(((entry->base +
							   (((bus - entry->start_bus_number) << 20) |
								(slot << 15) | (function << 12))) |
							  offset) +
							 MEM_PHYS_OFFSET);
				switch (access_size) {
					case 1: {
						return mminb(addr);
					}

					case 2: {
						return mminw(addr);
					}

					case 4: {
						return mmind(addr);
					}

					default:
						printf("PCI: Unknown access size: %hhu\n", access_size);
						break;
				}
			}
		}
	}

	printf("PCI: Tried to read from nonexistent device, %hx:%hhx:%hhx:%hhx\n",
		   seg, bus, slot, function);
	return 0;
}

static void mcfg_pci_write(uint16_t seg, uint8_t bus, uint8_t slot,
						   uint8_t function, uint16_t offset, uint32_t value,
						   uint8_t access_size) {
	for (int i = 0; i < mcfg_entries.length; i++) {
		struct mcfg_entry *entry = mcfg_entries.data[i];
		if (entry->seg == seg) {
			if (bus >= entry->start_bus_number &&
				bus <= entry->end_bus_number) {
				void *addr =
					(void *)(((entry->base +
							   (((bus - entry->start_bus_number) << 20) |
								(slot << 15) | (function << 12))) +
							  offset) +
							 MEM_PHYS_OFFSET);
				switch (access_size) {
					case 1: {
						mmoutb(addr, value);
						break;
					}

					case 2: {
						mmoutw(addr, value);
						break;
					}

					case 4: {
						mmoutd(addr, value);
						break;
					}

					default:
						printf("PCI: Unknown access size: %hhu\n", access_size);
						break;
				}
				return;
			}
		}
	}

	printf("PCI: Tried to write to nonexistent device, %hx:%hhx:%hhx:%hhx\n",
		   seg, bus, slot, function);
}

void pci_init(void) {
	struct mcfg *mcfg = (struct mcfg *)acpi_find_sdt("MCFG", 0);
	if (mcfg == NULL) {
		internal_read = legacy_pci_read;
		internal_write = legacy_pci_write;
	} else if (mcfg->header.length <
			   sizeof(struct mcfg) + sizeof(struct mcfg_entry)) {
		// There isn't any entry in the MCFG, assume legacy PCI
		internal_read = legacy_pci_read;
		internal_write = legacy_pci_write;
	} else {
		vec_init(&mcfg_entries);
		const size_t entries = (mcfg->header.length - sizeof(struct mcfg)) /
							   sizeof(struct mcfg_entry);
		for (size_t i = 0; i < entries; i++) {
			vec_push(&mcfg_entries, (void *)&mcfg->entries[i]);
		}

		internal_read = mcfg_pci_read;
		internal_write = mcfg_pci_write;
	}
}

uint32_t pci_read(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t function,
				  uint16_t offset, uint8_t access_size) {
	return internal_read(seg, bus, slot, function, offset, access_size);
}

void pci_write(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t function,
			   uint16_t offset, uint32_t value, uint8_t access_size) {
	internal_write(seg, bus, slot, function, offset, value, access_size);
}
