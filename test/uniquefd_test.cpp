#include "TestHarness.hpp"

#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include "t2t/util/UniqueFd.hpp"

using namespace abt;

namespace {

bool isOpen(int fd) {
    return ::fcntl(fd, F_GETFD) != -1;
}

int makeFd() {
    int p[2];
    CHECK_EQ(::pipe(p), 0);
    ::close(p[1]);
    return p[0];
}

void test_default_is_empty() {
    const util::UniqueFd fd;
    CHECK(!fd);
    CHECK_EQ(fd.get(), -1);
}

void test_closes_on_destruction() {
    const int raw = makeFd();
    {
        const util::UniqueFd fd{raw};
        CHECK(static_cast<bool>(fd));
        CHECK_EQ(fd.get(), raw);
        CHECK(isOpen(raw));
    }
    CHECK(!isOpen(raw));
}

void test_move_transfers_ownership() {
    const int      raw = makeFd();
    util::UniqueFd a{raw};
    util::UniqueFd b{std::move(a)};
    CHECK(!a);
    CHECK_EQ(b.get(), raw);
    CHECK(isOpen(raw));

    util::UniqueFd c;
    c = std::move(b);
    CHECK(!b);
    CHECK_EQ(c.get(), raw);
    CHECK(isOpen(raw));
}

void test_move_assign_closes_previous() {
    const int      first  = makeFd();
    const int      second = makeFd();
    util::UniqueFd a{first};
    util::UniqueFd b{second};
    a = std::move(b);
    CHECK(!isOpen(first));
    CHECK(isOpen(second));
    CHECK_EQ(a.get(), second);
}

void test_reset() {
    const int      first  = makeFd();
    const int      second = makeFd();
    util::UniqueFd fd{first};
    fd.reset(second);
    CHECK(!isOpen(first));
    CHECK(isOpen(second));
    fd.reset();
    CHECK(!fd);
    CHECK(!isOpen(second));
}

void test_release_gives_up_ownership() {
    const int raw = makeFd();
    int       out = -1;
    {
        util::UniqueFd fd{raw};
        out = fd.release();
        CHECK(!fd);
    }
    CHECK_EQ(out, raw);
    CHECK(isOpen(raw));
    ::close(raw);
}

void test_self_move_keeps_fd() {
    const int       raw = makeFd();
    util::UniqueFd  fd{raw};
    util::UniqueFd& alias = fd;
    fd                    = std::move(alias);
    CHECK_EQ(fd.get(), raw);
    CHECK(isOpen(raw));
}

}   // namespace

int main() {
    test_default_is_empty();
    test_closes_on_destruction();
    test_move_transfers_ownership();
    test_move_assign_closes_previous();
    test_reset();
    test_release_gives_up_ownership();
    test_self_move_keeps_fd();
    return abt::test::summary("uniquefd");
}
