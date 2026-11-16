#include <cstdio>
#include "test.h"
#include "core/kvfile.h"
#include "core/fs.h"
#include "core/rng.h"
#include "core/strutil.h"

TEST(kv_parse_and_types) {
    fd::KvFile k;
    k.parse("\xEF\xBB\xBF# comment\n[Section]\nname = Мики\nscale=0.72\r\nn=0x1F\nflag=on\nlist= a, b ,c\n; x\nbad line\n");
    CHECK_EQ(k.get("name"), std::string("Мики"));
    CHECK_EQ(k.getFloat("scale"), 0.72);
    CHECK_EQ(k.getInt("n"), 31LL);
    CHECK(k.getBool("flag"));
    CHECK_EQ(k.getList("list").size(), size_t(3));
    CHECK_EQ(k.getList("list")[1], std::string("b"));
    CHECK_EQ(k.getInt("missing", -5), -5LL);
    CHECK_EQ(k.size(), size_t(5));
}

TEST(kv_order_and_unknown_keys_kept) {
    fd::KvFile k;
    k.parse("schema=1\nfuture_key=42\nname=A\n");
    k.set("name", "B");
    k.setInt("bond", 3);
    CHECK_EQ(k.serialize(), std::string("schema=1\r\nfuture_key=42\r\nname=B\r\nbond=3\r\n"));
    k.erase("future_key");
    CHECK(!k.has("future_key"));
}

TEST(kv_atomic_save_with_backup) {
    const std::string p = "/tmp/fd_test_dog.dat";
    fd::fs::remove(p); fd::fs::remove(p + ".bak");
    fd::KvFile a; a.set("v", "1"); CHECK(a.save(p));
    fd::KvFile b; b.set("v", "2"); CHECK(b.save(p));
    fd::KvFile r; CHECK(r.load(p)); CHECK_EQ(r.get("v"), std::string("2"));
    fd::KvFile bak; CHECK(bak.load(p + ".bak")); CHECK_EQ(bak.get("v"), std::string("1"));
    CHECK(!fd::fs::exists(p + ".tmp"));
}

TEST(rng_deterministic_and_ranges) {
    fd::Rng a(7), b(7);
    for (int i = 0; i < 100; ++i) CHECK_EQ(a.next(), b.next());
    fd::Rng r(1);
    for (int i = 0; i < 1000; ++i) { int v = r.range(1, 4); CHECK(v >= 1 && v <= 4); }
    CHECK_EQ(r.weighted({0, 0, 0}), -1);
    CHECK_EQ(r.weighted({0, 5, 0}), 1);
}

TEST(str_helpers) {
    CHECK_EQ(fd::str::format("{name} sits, {name}!", {{"name", "Мики"}}), std::string("Мики sits, Мики!"));
    CHECK_EQ(fd::str::joaat("a_c_doghusky_01"), fd::str::joaat("A_C_DOGHUSKY_01"));
    CHECK_EQ(fd::str::joaat("PLAYER"), 0x6F0783F5u);
}
