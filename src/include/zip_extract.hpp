#pragma once
#include <cstddef>
#include <string>
namespace xs {
// Extrae UNA entrada (por nombre exacto) de un ZIP clasico (sin ZIP64 ni cifrado;
// metodos "stored" y "deflate"). Comprueba tamano y CRC32. No escribe fuera de out_path.
bool zip_extract_entry(const std::string& zip_path,const std::string& entry,const std::string& out_path,
                       size_t max_size,std::string& error);
}
