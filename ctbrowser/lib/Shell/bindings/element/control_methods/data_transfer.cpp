//===- data_transfer.cpp - DataTransfer, File and FileList ------*- C++ -*-===//
//
// FileList and DataTransfer, the two objects that carry files into a page's
// forms. There is no user here to pick a file or drag one in, so nothing in
// the engine fills them; what matters is that a page can: `new DataTransfer()`,
// `dt.items.add(file)`, then `input.files = dt.files`, which is how every
// file-submission test (and every drag-and-drop shim) builds its input.
//
// A FileList is an object on FileList.prototype holding its Files in a hidden
// array, with the indices as read-only own properties - enough for `files[0]`,
// `files.item(0)`, `files.length` and `for (const f of files)`. window.cpp
// defines the FileList global (p5 insists it exists); its `prototype` is
// completed here, so `input.files instanceof FileList` holds.
//
//===----------------------------------------------------------------------===//

#include "helpers.hpp"

#include <string>
#include <vector>

namespace ctbrowser::shell::detail {

namespace {

constexpr std::string_view files_key = CTBROWSER_PRIVATE_KEY("ctbrowser:file-list");
constexpr std::string_view dt_items_key = CTBROWSER_PRIVATE_KEY("ctbrowser:data-transfer-items");
constexpr std::string_view dt_list_key = CTBROWSER_PRIVATE_KEY("ctbrowser:data-transfer-item-list");
constexpr std::string_view owner_key = CTBROWSER_PRIVATE_KEY("ctbrowser:data-transfer-owner");
constexpr std::string_view dt_files_key = CTBROWSER_PRIVATE_KEY("ctbrowser:data-transfer-files");

[[nodiscard]] script::array_object * as_array(value v) {
    return v.is_array() ? static_cast<script::array_object *>(v.as_heap()) : nullptr;
}

/// The hidden array a FileList or a DataTransfer keeps its entries in.
[[nodiscard]] script::array_object * entries_of(value holder, std::string_view key) {
    script::object_object * object = object_of(holder);
    if (object == nullptr) { return nullptr; }
    const value * held = object->find(std::string{key});
    return held == nullptr ? nullptr : as_array(*held);
}

/// FileList.prototype, completed once: item(), a length getter and the array
/// iterator over the hidden list.
[[nodiscard]] value file_list_prototype(context & c) {
    const value ctor = c.global("FileList");
    value proto = ctor.is_object_like() ? c.lookup_property(ctor, "prototype") : value::undefined();
    if (!proto.is_object()) {
        proto = c.make_object();
        if (ctor.is_object_like()) { c.store_property(ctor, "prototype", proto); }
    }
    script::object_object & on = *object_of(proto);
    if (on.find("item") != nullptr) { return proto; }
    set_method(c, on, "item", [](context & cx, std::span<value> a) {
        script::array_object * files = entries_of(cx.current_this(), files_key);
        const double index = cx.to_number(arg(a, 0));
        if (files == nullptr || !(index >= 0) ||
            index >= static_cast<double>(files->items.size())) {
            return value::null();
        }
        return files->items[static_cast<std::size_t>(index)];
    });
    define_getter(c, on, "length", [](context & cx, std::span<value>) {
        script::array_object * files = entries_of(cx.current_this(), files_key);
        return value::number(files == nullptr ? 0 : static_cast<double>(files->items.size()));
    });
    on.define(CTBROWSER_SYMBOL_KEY("iterator"),
              native(c, "values",
                     [](context & cx, std::span<value>) {
                         const value self = cx.current_this();
                         const value * held = object_of(self) == nullptr
                                                  ? nullptr
                                                  : object_of(self)->find(std::string{files_key});
                         const value list = held == nullptr ? cx.make_array() : *held;
                         const value values = cx.lookup_property(list, "values");
                         return values.is_callable() ? cx.call(values, {}, list)
                                                     : value::undefined();
                     }),
              script::attr_builtin);
    on.define(CTBROWSER_SYMBOL_KEY("toStringTag"), c.string("FileList"), script::attr_configurable);
    return proto;
}

/// Whether \p v is a File (a Blob with a name), the only thing a FileList or
/// a file item of a transfer can hold.
[[nodiscard]] bool is_file(context & c, value v) {
    if (!v.is_object()) { return false; }
    const value file_ctor = c.global("File");
    const value blob_ctor = c.global("Blob");
    if (file_ctor.is_callable() && c.instance_of(v, file_ctor)) {
        return object_of(v)->find("name") != nullptr;
    }
    return blob_ctor.is_callable() && c.instance_of(v, blob_ctor) &&
           object_of(v)->find("name") != nullptr;
}

} // namespace

namespace {

/// Rewrites a FileList's indices and hidden array to hold \p files.
void fill_file_list(context & c, script::object_object & list, const std::vector<value> & files) {
    for (std::size_t i = 0;; ++i) {
        if (list.find(std::to_string(i)) == nullptr) { break; }
        (void)list.erase(std::to_string(i));
    }
    const value held = c.make_array();
    for (std::size_t i = 0; i < files.size(); ++i) {
        as_array(held)->items.push_back(files[i]);
        list.define(std::to_string(i), files[i], script::attr_enumerable);
    }
    list.define(std::string{files_key}, held, script::attr_none);
}

/// The Files among a transfer's items, in order.
[[nodiscard]] std::vector<value> transfer_files(context & c, value transfer) {
    std::vector<value> files;
    if (script::array_object * records = entries_of(transfer, dt_items_key)) {
        for (const value & record : records->items) {
            if (c.to_string(c.lookup_property(record, "kind")) == "file") {
                files.push_back(c.lookup_property(record, "data"));
            }
        }
    }
    return files;
}

} // namespace

value make_file_list(context & c, const std::vector<value> & files) {
    auto * list = c.allocate<script::object_object>();
    list->prototype = file_list_prototype(c);
    fill_file_list(c, *list, files);
    return value::object(list);
}

bool is_file_list(value v) {
    script::object_object * object = object_of(v);
    return object != nullptr && object->find(std::string{files_key}) != nullptr;
}

std::vector<value> files_in(value list) {
    script::array_object * files = entries_of(list, files_key);
    return files == nullptr ? std::vector<value>{}
                            : std::vector<value>(files->items.begin(), files->items.end());
}

// --- DataTransfer, HTML 6.11.3 -----------------------------------------------------
//
// An item is a plain record {kind, type, data} in the transfer's hidden array;
// `items` is the one DataTransferItemList over it ([SameObject]), whose indices
// are refreshed on every add, remove and clear, and `files` a FileList of the
// file items at the moment of asking.
void control_helpers::install_data_transfer(context & cx) const {
    if (cx.has_global("DataTransfer")) { return; }
    dom_bindings * b = bindings;
    auto * dt_proto = cx.allocate<script::object_object>();
    auto * list_proto = cx.allocate<script::object_object>();
    auto * item_proto = cx.allocate<script::object_object>();
    const value dt_proto_value = value::object(dt_proto);
    const value list_proto_value = value::object(list_proto);
    const value item_proto_value = value::object(item_proto);

    // The item list's own indices, rebuilt from the transfer's records: each
    // a DataTransferItem carrying its kind and type.
    const auto refresh = [item_proto_value](context & c, value list) {
        script::object_object * on = object_of(list);
        if (on == nullptr) { return; }
        const value * owner = on->find(std::string{owner_key});
        script::array_object * records =
            owner == nullptr ? nullptr : entries_of(*owner, dt_items_key);
        for (std::size_t i = 0;; ++i) {
            if (on->find(std::to_string(i)) == nullptr) { break; }
            (void)on->erase(std::to_string(i));
        }
        if (records == nullptr) { return; }
        // The transfer's FileList is live too: the one `files` handed out
        // follows the items, including when it was assigned to an <input>.
        if (script::object_object * transfer = object_of(*owner)) {
            if (const value * files = transfer->find(std::string{dt_files_key});
                files != nullptr && object_of(*files) != nullptr) {
                fill_file_list(c, *object_of(*files), transfer_files(c, *owner));
            }
        }
        for (std::size_t i = 0; i < records->items.size(); ++i) {
            auto * item = c.allocate<script::object_object>();
            item->prototype = item_proto_value;
            item->define(CTBROWSER_PRIVATE_KEY("record"), records->items[i], script::attr_none);
            item->define("kind", c.lookup_property(records->items[i], "kind"),
                         script::attr_enumerable);
            item->define("type", c.lookup_property(records->items[i], "type"),
                         script::attr_enumerable);
            on->define(std::to_string(i), value::object(item), script::attr_enumerable);
        }
    };
    const auto records_of_list = [](context & c) -> script::array_object * {
        script::object_object * on = object_of(c.current_this());
        const value * owner = on == nullptr ? nullptr : on->find(std::string{owner_key});
        return owner == nullptr ? nullptr : entries_of(*owner, dt_items_key);
    };

    define_getter(cx, *list_proto, "length", [records_of_list](context & c, std::span<value>) {
        script::array_object * records = records_of_list(c);
        return value::number(records == nullptr ? 0 : static_cast<double>(records->items.size()));
    });
    // add(data, type) for a string, add(file) for a File: the new item, or
    // null; a second string item of one type is a NotSupportedError.
    set_method(
        cx, *list_proto, "add", [b, records_of_list, refresh](context & c, std::span<value> a) {
            script::array_object * records = records_of_list(c);
            if (records == nullptr) { return value::null(); }
            const value data = arg(a, 0);
            auto * record = c.allocate<script::object_object>();
            if (is_file(c, data)) {
                record->set("kind", c.string("file"));
                record->set("type", c.string(c.to_string(c.lookup_property(data, "type"))));
                record->set("data", data);
            } else {
                if (a.size() < 2) {
                    c.throw_error("TypeError", "DataTransferItemList.add: a string needs a type");
                    return value::undefined();
                }
                const std::string type = ascii_lower_copy(c.to_string(a[1]));
                for (const value & held : records->items) {
                    if (c.to_string(c.lookup_property(held, "kind")) == "string" &&
                        c.to_string(c.lookup_property(held, "type")) == type) {
                        b->throw_dom_exception(
                            c, "NotSupportedError",
                            "DataTransferItemList.add: an item of that type exists");
                        return value::undefined();
                    }
                }
                record->set("kind", c.string("string"));
                record->set("type", c.string(type));
                record->set("data", c.string(c.to_string(data)));
            }
            records->items.push_back(value::object(record));
            const value self = c.current_this();
            refresh(c, self);
            return c.lookup_index(self,
                                  value::number(static_cast<double>(records->items.size() - 1)));
        });
    set_method(
        cx, *list_proto, "remove", [records_of_list, refresh](context & c, std::span<value> a) {
            script::array_object * records = records_of_list(c);
            const double index = c.to_number(arg(a, 0));
            if (records != nullptr && index >= 0 &&
                index < static_cast<double>(records->items.size())) {
                records->items.erase(records->items.begin() + static_cast<std::ptrdiff_t>(index));
                refresh(c, c.current_this());
            }
            return value::undefined();
        });
    set_method(cx, *list_proto, "clear", [records_of_list, refresh](context & c, std::span<value>) {
        if (script::array_object * records = records_of_list(c)) {
            records->items.clear();
            refresh(c, c.current_this());
        }
        return value::undefined();
    });
    list_proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("DataTransferItemList"),
                       script::attr_configurable);

    set_method(cx, *item_proto, "getAsFile", [](context & c, std::span<value>) {
        script::object_object * self = object_of(c.current_this());
        const value * record =
            self == nullptr ? nullptr : self->find(CTBROWSER_PRIVATE_KEY("record"));
        if (record == nullptr || c.to_string(c.lookup_property(*record, "kind")) != "file") {
            return value::null();
        }
        return c.lookup_property(*record, "data");
    });
    item_proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("DataTransferItem"),
                       script::attr_configurable);

    // The transfer itself.
    define_getter(cx, *dt_proto, "items", [](context & c, std::span<value>) {
        script::object_object * self = object_of(c.current_this());
        const value * list = self == nullptr ? nullptr : self->find(std::string{dt_list_key});
        return list == nullptr ? value::undefined() : *list;
    });
    // `files` is [SameObject] and live: one FileList per transfer, made on the
    // first read and kept in step with the items from then on.
    define_getter(cx, *dt_proto, "files", [](context & c, std::span<value>) {
        const value self = c.current_this();
        script::object_object * transfer = object_of(self);
        if (transfer == nullptr) { return make_file_list(c, {}); }
        if (const value * held = transfer->find(std::string{dt_files_key}); held != nullptr) {
            return *held;
        }
        const value made = make_file_list(c, transfer_files(c, self));
        transfer->define(std::string{dt_files_key}, made, script::attr_none);
        return made;
    });
    define_getter(cx, *dt_proto, "types", [](context & c, std::span<value>) {
        const value out = c.make_array();
        bool any_file = false;
        if (script::array_object * records = entries_of(c.current_this(), dt_items_key)) {
            for (const value & record : records->items) {
                if (c.to_string(c.lookup_property(record, "kind")) == "file") {
                    any_file = true;
                } else {
                    as_array(out)->items.push_back(c.lookup_property(record, "type"));
                }
            }
        }
        if (any_file) { as_array(out)->items.push_back(c.string("Files")); }
        return out;
    });
    // getData / setData / clearData over the string items, formats lowercased
    // and "text" meaning "text/plain", "url" "text/uri-list".
    const auto format_of = [](context & c, value given) {
        std::string format = ascii_lower_copy(c.to_string(given));
        if (format == "text") { format = "text/plain"; }
        if (format == "url") { format = "text/uri-list"; }
        return format;
    };
    set_method(cx, *dt_proto, "getData", [format_of](context & c, std::span<value> a) {
        const std::string format = format_of(c, arg(a, 0));
        if (script::array_object * records = entries_of(c.current_this(), dt_items_key)) {
            for (const value & record : records->items) {
                if (c.to_string(c.lookup_property(record, "kind")) == "string" &&
                    c.to_string(c.lookup_property(record, "type")) == format) {
                    return c.lookup_property(record, "data");
                }
            }
        }
        return c.string("");
    });
    set_method(cx, *dt_proto, "setData", [format_of, refresh](context & c, std::span<value> a) {
        const std::string format = format_of(c, arg(a, 0));
        script::array_object * records = entries_of(c.current_this(), dt_items_key);
        if (records == nullptr) { return value::undefined(); }
        std::erase_if(records->items, [&](const value & record) {
            return c.to_string(c.lookup_property(record, "kind")) == "string" &&
                   c.to_string(c.lookup_property(record, "type")) == format;
        });
        auto * record = c.allocate<script::object_object>();
        record->set("kind", c.string("string"));
        record->set("type", c.string(format));
        record->set("data", c.string(c.to_string(arg(a, 1))));
        records->items.push_back(value::object(record));
        script::object_object * self = object_of(c.current_this());
        if (const value * list = self->find(std::string{dt_list_key})) { refresh(c, *list); }
        return value::undefined();
    });
    set_method(cx, *dt_proto, "clearData", [format_of, refresh](context & c, std::span<value> a) {
        script::array_object * records = entries_of(c.current_this(), dt_items_key);
        if (records == nullptr) { return value::undefined(); }
        const bool all = arg(a, 0).is_undefined();
        const std::string format = all ? std::string{} : format_of(c, arg(a, 0));
        std::erase_if(records->items, [&](const value & record) {
            return c.to_string(c.lookup_property(record, "kind")) == "string" &&
                   (all || c.to_string(c.lookup_property(record, "type")) == format);
        });
        script::object_object * self = object_of(c.current_this());
        if (const value * list = self->find(std::string{dt_list_key})) { refresh(c, *list); }
        return value::undefined();
    });
    dt_proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("DataTransfer"),
                     script::attr_configurable);

    auto * ctor = cx.allocate<script::native_object>(
        "DataTransfer", [dt_proto_value, list_proto_value](context & c, std::span<value>) -> value {
            const value self = c.current_this();
            script::object_object * made = object_of(self);
            if (made == nullptr) {
                c.throw_error("TypeError",
                              "Failed to construct 'DataTransfer': please use the 'new' operator.");
                return value::undefined();
            }
            if (!made->prototype.is_object()) { made->prototype = dt_proto_value; }
            made->define(std::string{dt_items_key}, c.make_array(), script::attr_none);
            auto * list = c.allocate<script::object_object>();
            list->prototype = list_proto_value;
            list->define(std::string{owner_key}, self, script::attr_none);
            made->define(std::string{dt_list_key}, value::object(list), script::attr_none);
            made->set("dropEffect", c.string("none"));
            made->set("effectAllowed", c.string("none"));
            return self;
        });
    // The item-list and item prototypes are reachable only through the lambdas
    // above, and a C++ capture is not a GC root, so the constructor retains
    // them explicitly (native_object::retained); otherwise the collector frees
    // them before the first DataTransfer is made.
    ctor->retained.push_back(list_proto_value);
    ctor->retained.push_back(item_proto_value);
    ctor->define("prototype", dt_proto_value, script::attr_none);
    ctor->define("length", value::number(0), script::attr_configurable);
    dt_proto->define("constructor", value::object(ctor), script::attr_builtin);
    cx.define_builtin_global("DataTransfer", value::object(ctor));
}

} // namespace ctbrowser::shell::detail
