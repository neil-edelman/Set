/** @license 2019 Neil Edelman, distributed under the terms of the
 [MIT License](https://opensource.org/licenses/MIT).

 <tag:<E>Set> is a collection of elements of <tag:<E>SetElement> that doesn't
 allow duplication, when supplied an equality function, `SET_IS_EQUAL`
 <typedef:<PE>IsEqual>, and a hash function, `SET_HASH` <typedef:<PE>Hash>. If
 the hash function distributes elements uniformly, it allows lookup, insertion,
 and deletion, of `E`, in average \O(1).

 Internally, it is a simple, separately chained, hash set with a maximum load
 factor of `ln 2` and power-of-two resizes, with buckets as pointers. This
 offers some independence of sets from set elements, but cache performance is
 left up to the caller. It can be expanded to a hash map or associative array
 by enclosing the `<E>SetElement` in another parent `struct`, as appropriate.
 While in a set, the elements should not change in a way that affects their
 hash values. The keys cannot be entirely polymorphic across the boundary of
 `E` because <fn:<E>SetGet> requires `E` to be instantiatable.

 ![Example of <String>Set.](../image/index.png)

 @param[SET_NAME, SET_TYPE]
 `E` that satisfies `C` naming conventions when mangled and a valid type
 associated therewith; required.

 @param[SET_HASH]
 A function satisfying <typedef:<PE>Hash>; required.

 @param[SET_IS_EQUAL]
 A function satisfying <typedef:<PE>IsEqual>; required.

 @param[SET_TO_STRING]
 Optional print function implementing <typedef:<PE>ToString>; makes available
 <fn:<E>SetToString>.

 @param[SET_PASS_POINTER]
 Changes `PE` from `E` to `E *`; used in `SET_HASH` and `SET_IS_EQUAL`. Should
 be used when `E` is a `struct` whose copying into functions is a performance
 issue.

 @param[SET_NO_CACHE]
 Should be used when the hash calculation is trivial to avoid storing duplicate
 information _per_ datum. Enabled, it always calculates the hash and discards
 it. Using non-randomly-distributed data directly as a hash is not ostensibly
 sound, but in certain situations, it leads to a more balanced table.

 @param[SET_HASH_TYPE]
 This is <typedef:<PE>UInt>. If `SET_NO_CACHE` is not set, stored _per_ datum.
 Defaults to `unsigned`, but one can change it to any unsigned integer type.
 The hash map will saturate at `((ln 2) / 2) \cdot range(<PE>UInt)`, at which
 point no new buckets can be added and the load factor can go over the maximum.

 @param[SET_TEST]
 Unit testing framework, included in a separate header, <../test/SetTest.h>.
 Must be defined equal to a random filler function, satisfying
 <typedef:<PE>Action>. Requires `SET_TO_STRING` and not `NDEBUG`.

 @std C89/90 */

#include <stddef.h>	/* offsetof */
#include <limits.h> /* SIZE_MAX? */
#include <stdlib.h> /* realloc free */
#include <assert.h>	/* assert */
#include <errno.h>  /* errno */
#ifdef SET_TO_STRING /* <!-- string */
#include <string.h> /* strlen memcpy */
#endif /* string --> */



/* Check defines. */
#ifndef SET_NAME
#error Generic SET_NAME undefined.
#endif
#ifndef SET_TYPE
#error Tag SET_TYPE undefined.
#endif
#ifndef SET_IS_EQUAL
#error Function SET_IS_EQUAL undefined.
#endif
#ifndef SET_HASH
#error Function SET_HASH undefined.
#endif
#if defined(SET_TEST) && !defined(SET_TO_STRING)
#error SET_TEST requires SET_TO_STRING.
#endif
/* <https://stackoverflow.com/q/44401965> */
#ifdef SIZE_MAX
#define SET_SIZE_MAX SIZE_MAX
#else
#define SET_SIZE_MAX ((size_t)(-1))
#endif



/* Generics using the preprocessor;
 <http://stackoverflow.com/questions/16522341/pseudo-generics-in-c>. */
#ifdef CAT
#undef CAT
#endif
#ifdef CAT_
#undef CAT_
#endif
#ifdef PCAT
#undef PCAT
#endif
#ifdef PCAT_
#undef PCAT_
#endif
#ifdef E
#undef E
#endif
#ifdef PE /* pointer-to-E */
#undef PE
#endif
#ifdef E_
#undef E_
#endif
#ifdef PE_ /* private-E */
#undef PE_
#endif
#define CAT_(x, y) x ## y
#define CAT(x, y) CAT_(x, y)
#define PCAT_(x, y) x ## _ ## y
#define PCAT(x, y) PCAT_(x, y)
#define E_(thing) CAT(SET_NAME, thing)
#define PE_(thing) PCAT(set, PCAT(SET_NAME, thing)) /* "Private." */

/* Check `SET_TYPE` is a valid type, whose definition is placed above
 inclusion. */
typedef SET_TYPE PE_(Type);
#ifdef SET_PASS_POINTER /* <!-- pointer */
typedef PE_(Type) const* PE_(PType);
#else /* pointer --><!-- !pointer */
typedef PE_(Type) PE_(PType);
#endif /* !pointer --> */
#define E PE_(Type)
#define PE PE_(PType)



#ifdef SET_HASH_TYPE /* <!-- hash type */
/** Valid unsigned integer type; defaults to `unsigned int`. */
typedef SET_HASH_TYPE PE_(UInt);
#else /* hash type --><!-- !hash type */
typedef unsigned PE_(UInt);
#endif /* !hash type --> */

/** Contains `E` as the element `data` along with data internal to the set.
 Storage of the `<E>SetElement` structure is the responsibility of the
 caller. */
struct E_(SetElement);
struct E_(SetElement) {
	struct E_(SetElement) *next;
#ifndef SET_NO_CACHE /* <!-- cache */
	PE_(UInt) hash;
#endif /* cache --> */
	E data;
};

/* Singly-linked list head for `buckets`. Not really needed, but
 double-pointers are confusing. */
struct PE_(Bucket) { struct E_(SetElement) *first; };

/** An `<E>Set`. To initialise, see <fn:<E>Set>. */
struct E_(Set);
struct E_(Set) {
	struct PE_(Bucket) *buckets; /* An array of 1 << log_capacity (>3) or 0. */
	size_t size;
	unsigned log_capacity;
};
#define SET_ZERO { 0, 0, 0 }



/** A map from `E` onto <typedef:<PE>UInt>, (defaults to `unsigned`). Should be
 as close as possible to a discrete uniform distribution for maximum
 performance. (`<PE>` is private `E`, one will have to redeclare it to match if
 one needs it; `PE` is, if `SET_PASS_POINTER`, `E *`, and if not `E`.) */
typedef PE_(UInt) (*PE_(Hash))(const PE);
/* Check that `SET_HASH` is a function implementing <typedef:<PE>Hash>. */
static const PE_(Hash) PE_(hash) = (SET_HASH);

/** A constant equivalence relation between `E` that satisfies
 `<PE>IsEqual(a, b) -> <PE>Hash(a) == <PE>Hash(b)`. `PE` depends on
 `SET_PASS_POINTER`. */
typedef int (*PE_(IsEqual))(const PE, const PE);
/* Check that `SET_IS_EQUAL` is a function implementing
 <typedef:<PE>IsEqual>. */
static const PE_(IsEqual) PE_(equal) = (SET_IS_EQUAL);

/** Returns true if the `replace` replaces the `original`; used in
 <fn:<E>SetPolicyPut>. */
typedef int (*PE_(Replace))(E *original, E *replace);

#ifdef SET_TO_STRING /* <!-- string */
/** Responsible for turning `E` (the first argument) into a 12 `char` string
 (the second.) */
typedef void (*PE_(ToString))(const E *const, char (*const)[12]);
/* Check that `SET_TO_STRING` is a function implementing
 <typedef:<PE>ToString>. */
static const PE_(ToString) PE_(to_string) = (SET_TO_STRING);
#endif /* string --> */

#ifdef SET_TEST /* <!-- test */
/** Used for `SET_TEST`. */
typedef void (*PE_(Action))(E *const);
#endif /* test --> */



#ifdef SET_PASS_POINTER /* <!-- pointer */
/** @return `element`. */
static const E *PE_(pointer)(const E *const element) { return element; }
#else /* pointer --><!-- !pointer */
/** @return Re-de-reference `element`. */
static E PE_(pointer)(const E *const element) { return *element; }
#endif /* !pointer --> */

/** Gets the hash of `element`. */
static PE_(UInt) PE_(get_hash)(struct E_(SetElement) *element) {
	assert(element);
#ifdef SET_NO_CACHE /* <!-- !cache */
	return PE_(hash)(PE_(pointer)(&element->data));
#else /* !cache --><!-- cache */
	return element->hash;
#endif /* cache --> */
}

/** Retrieves a bucket from `set` given the `hash`. Only call this function if
 non-empty. May be invalidated upon a call to <fn:<PE>grow>.
 @return Given a `hash`, compute the bucket at it's index. */
static struct PE_(Bucket) *PE_(get_bucket)(struct E_(Set) *const set,
	const PE_(UInt) hash) {
	assert(set && set->buckets);
	return set->buckets + (hash & ((1 << set->log_capacity) - 1));
}

/** Linear search for `data` in `bucket`.
 @param[hash] Must match the hash of `data`.
 @return The link that points to the `data` or null. */
static struct E_(SetElement) **PE_(bucket_to)(struct PE_(Bucket) *const bucket,
	const PE_(UInt) hash, const PE data) {
	struct E_(SetElement) **to_x, *x;
	assert(bucket);
	for(to_x = &bucket->first; (x = *to_x); to_x = &x->next) {
#ifndef SET_NO_CACHE /* <!-- cache: a quick out. */
		if(hash != x->hash) continue;
#endif /* cache --> */
		if(PE_(equal)(data, PE_(pointer)(&x->data))) return to_x;
	}
#ifdef SET_NO_CACHE /* <!-- !cache */
	(void)(hash);
#endif /* cache --> */
	return 0;
}

/** Private: grow the `set` until the capacity is at least `size`.
 @return Success; otherwise, `errno` may be set.
 @throws[ERANGE] Tried allocating more then can fit in `size_t`.
 @throws[realloc]
 @order Amortized \O(1). */
static int PE_(grow)(struct E_(Set) *const set, const size_t size) {
	struct PE_(Bucket) *buckets, *b, *b_end, *new_b;
	struct E_(SetElement) **to_x, *x;
	const unsigned log_c0 = set->log_capacity,
		log_limit = sizeof(PE_(UInt)) * 8 - 1;
	unsigned log_c1;
	PE_(UInt) c0 = 1 << log_c0, c1, mask;
	size_t no_buckets;
	/* One did set `<PE>UInt` to an unsigned type, right? */
	assert(set && c0 && log_c0 <= log_limit && (log_c0 >= 3 || !log_c0)
		&& (PE_(UInt))-1 > 0);
	/* `SIZE_MAX` min 65535 -> 5041 but typically much larger _st_ it becomes
	 saturated while the load factor increases. */
	if(size > SET_SIZE_MAX / 13) return errno = ERANGE, 0;
	/* Load factor `0.693147180559945309417232121458176568 ~= 9/13`.
	 Starting bucket number is a power of 2 in `[8, 1 << log_limit]`. */
	if((no_buckets = size * 13 / 9) > 1u << log_limit) {
		log_c1 = log_limit;
		c1 = 1 << log_limit;
	} else {
		if(log_c0 < 3) log_c1 = 3u,     c1 = 8u;
		else           log_c1 = log_c0, c1 = c0;
		while(c1 < no_buckets) log_c1++, c1 <<= 1;
	}
	/* It's under the critical load factor; don't need to do anything. */
	if(log_c0 == log_c1) return 1;
	/* Everything else is amortised. Allocate new space for an expansion. */
	if(!(buckets = realloc(set->buckets, sizeof *buckets * c1))) return 0;
	set->buckets = buckets;
	set->log_capacity = log_c1;
	/* The mask needs domain `c0 \in [1, max]`, but we want 0 for loops. */
	mask = (c1 - 1) ^ (c0 - 1), assert(mask);
	if(c0 == 1) c0 = 0, assert(!c0 || c0 >= 8);
	/* Initialize the new lists to contain no elements. */
	for(b = buckets + c0, b_end = buckets + c1; b < b_end; b++) b->first = 0;
	/* The expectation value of the fraction of entries that needs to be
	 rehashed is 1/2 _vs_ any other way would require a complete rehash. */
	for(b = buckets, b_end = buckets + c0; b < b_end; b++) {
		PE_(UInt) hash;
		assert(!((b - buckets) & mask));
		to_x = &b->first;
		while(*to_x) {
			hash = PE_(get_hash)((x = *to_x));
			if(!(hash & mask)) { to_x = &x->next; continue; }
			*to_x = x->next; /* Remove. */
			new_b = PE_(get_bucket)(set, hash);
			x->next = new_b->first, new_b->first = x;
		}
	}
	return 1;
}

/** Most general put function that every put function calls. Puts `element` in
 `set` and returns the collided element, if any, as long as `replace` is null
 or returns 1. */
static struct E_(SetElement) *PE_(put)(struct E_(Set) *const set,
	struct E_(SetElement) *const element, const PE_(Replace) replace) {
	struct PE_(Bucket) *bucket;
	struct E_(SetElement) **to_x = 0, *x = 0;
	PE_(UInt) hash;
	if(!set || !element) return 0;
	hash = PE_(hash)(PE_(pointer)(&element->data));
#ifndef SET_NO_CACHE /* <!-- cache */
	element->hash = hash;
#endif /* cache --> */
	/* Delete any duplicate. */
	if(set->buckets) {
		bucket = PE_(get_bucket)(set, hash);
		if((to_x = PE_(bucket_to)(bucket, hash, PE_(pointer)(&element->data))))
		{
			x = *to_x;
			if(replace && !replace(&x->data, &element->data)) return 0;
			*to_x = x->next, x->next = 0;
			goto erased;
		}
	}
	/* New entry; the bucket may change. */
	assert(set->size + 1 > set->size);
	if(!PE_(grow)(set, set->size + 1)) return 0; /* Didn't <fn:<E>SetReserve>.*/
	bucket = PE_(get_bucket)(set, hash);
	set->size++;
erased:
	/* Stick the element on the head of the bucket. */
	element->next = bucket->first, bucket->first = element;
	return x;
}

/** Used in <fn:<E>SetPolicyPut> when `replace` is null; `original` and
 `replace` are ignored. */
static int PE_(false)(E *original, E *replace) {
	(void)(original); (void)(replace);
	return 0;
}

/** Zeros `set`, a well-defined state. */
static void PE_(set)(struct E_(Set) *const set) {
	assert(set);
	set->buckets      = 0;
	set->log_capacity = 0;
	set->size         = 0;
}

/** Destructor for `set`. After, it takes no memory and is in an empty state.
 @allow */
static void E_(Set_)(struct E_(Set) *const set) {
	if(!set) return;
	free(set->buckets);
	PE_(set)(set);
}

/** Initialises `set` to be take no memory and be in an empty state.
 Alternatively, assigning `{0}` (`C99`+) or `SET_ZERO` as the initialiser, or
 being part of `static` data, also puts it in an empty state. Calling this on
 an active set will cause memory leaks.
 @param[set] If null, does nothing.
 @order \Theta(1)
 @allow */
static void E_(Set)(struct E_(Set) *const set) {
	if(!set) return;
	PE_(set)(set);
}

/** Clears and removes all entries from `set`. The capacity and memory of the
 hash table is preserved, but all previous values are un-associated. The load
 factor will be less until it reaches it's previous size.
 @param[set] If null, does nothing.
 @order \Theta(`set.buckets`)
 @allow */
static void E_(SetClear)(struct E_(Set) *const set) {
	struct PE_(Bucket) *b, *b_end;
	if(!set || !set->log_capacity) return;
	for(b = set->buckets, b_end = b + (1 << set->log_capacity); b < b_end; b++)
		b->first = 0;
	set->size = 0;
}

/** @return The number of entries in the `set`.
 @param[set] If null, returns 0.
 @order \Theta(1)
 @allow */
static size_t E_(SetSize)(const struct E_(Set) *const set) {
	if(!set) return 0;
	return set->size;
}

/** Queries whether `data` is is `set`.
 @param[set] If null, returns null.
 @return The value which <typedef:<PE>IsEqual> `data`, or, if no such value
 exists, null.
 @order Average \O(1), (hash distributes elements uniformly); worst \O(n).
 @allow */
static struct E_(SetElement) *E_(SetGet)(struct E_(Set) *const set,
	const PE data) {
	struct E_(SetElement) **to_x;
	PE_(UInt) hash;
	if(!set || !set->buckets) return 0;
	hash = PE_(hash)(data);
	to_x = PE_(bucket_to)(PE_(get_bucket)(set, hash), hash, data);
	return to_x ? *to_x : 0;
}

/** Reserve at least `reserve`, divided by the maximum load factor, space in
 the buckets of `set`.
 @return Success.
 @throws[ERANGE] `reserve` plus the size would take a bigger number then could
 fit in a `size_t`.
 @throws[realloc]
 @allow */
static int E_(SetReserve)(struct E_(Set) *const set, const size_t reserve) {
	if(!set) return 0;
	if(reserve > SET_SIZE_MAX - set->size) return errno = ERANGE, 0;
	return PE_(grow)(set, set->size + reserve);
}

/** Puts the `element` in `set`.
 @param[set, element] If null, returns false.
 @param[element] Should not be of a `set` because the integrity of that `set`
 will be compromised.
 @return Adding an element with the same `E`, according to
 <typedef:<PE>IsEqual> `SET_IS_EQUAL`, causes the old data to be ejected and
 returned, otherwise null.
 @throws[realloc, ERANGE] There was an error with a re-sizing. Calling
 <fn:<E>SetReserve> before ensures that this does not happen.
 @order Average amortised \O(1), (hash distributes elements uniformly);
 worst \O(n).
 @allow */
static struct E_(SetElement) *E_(SetPut)(struct E_(Set) *const set,
	struct E_(SetElement) *const element) {
	return PE_(put)(set, element, 0);
}

/** Puts the `element` in `set` only if the entry is absent or if calling
 `replace` returns true.
 @param[set, element] If null, returns false.
 @param[element] Must not be part this `set` or any other.
 @param[replace] If specified, gets called on collision and only replaces it if
 the function returns true. If null, doesn't do any replacement on collision.
 @return Any ejected element or null.
 @throws[realloc, ERANGE] There was an error with a re-sizing. Calling
 <fn:<E>SetReserve> before ensures that this does not happen.
 @order Average amortised \O(1), (hash distributes elements uniformly);
 worst \O(n).
 @allow */
static struct E_(SetElement) *E_(SetPolicyPut)(struct E_(Set) *const set,
	struct E_(SetElement) *const element, const PE_(Replace) replace) {
	return PE_(put)(set, element, replace ? replace : &PE_(false));
}

/** Removes an element `data` from `set`.
 @return Successfully removed element or null. This element is free to be put
 into another set.
 @order Average \O(1), (hash distributes elements uniformly); worst \O(n).
 @allow */
static struct E_(SetElement) *E_(SetRemove)(struct E_(Set) *const set,
	const PE data) {
	PE_(UInt) hash;
	struct E_(SetElement) **to_x, *x;
	if(!set || !set->buckets) return 0;
	hash = PE_(hash)(data);
	if(!(to_x = PE_(bucket_to)(PE_(get_bucket)(set, hash), hash, data)))
		return 0;
	x = *to_x;
	*to_x = x->next;
	assert(set->size);
	set->size--;
	return x;
}

#ifdef SET_TO_STRING /* <!-- print */

/** Can print 2 things at once before it overwrites. One must set
 `SET_TO_STRING` to a function implementing <typedef:<PE>ToString> to get this
 functionality.
 @return Prints `set` in a static buffer in order by bucket.
 @order \Theta(1); it has a 1024 character limit; every element takes some of
 it.
 @allow */
static const char *E_(SetToString)(const struct E_(Set) *const set) {
	static char strings[2][1024];
	static size_t strings_i;
	char *string = strings[strings_i++], *s = string;
	const size_t strings_no = sizeof strings / sizeof *strings,
		string_size = sizeof *strings / sizeof **strings;
	const char space = ' ', start = '{', comma = ',', end = '}',
		*const ellipsis_end = ",…}", *const null = "null";
	const size_t ellipsis_end_len = strlen(ellipsis_end),
		null_len = strlen(null);
	struct PE_(Bucket) *b, *b_end;
	size_t i;
	int is_first = 1;
	assert(!(strings_no & (strings_no - 1)) && ellipsis_end_len >= 2
		&& string_size >= 2 + 11 + ellipsis_end_len + 1
		&& string_size >= null_len + 1);
	/* Advance the buffer for next time. */
	strings_i &= strings_no - 1;
	/* Null set. */
	if(!set) { memcpy(s, null, null_len), s += null_len; goto terminate; }
	/* Otherwise */
	*s++ = start;
	if(!set->buckets) goto end_buckets;
	for(b = set->buckets, b_end = b + (1 << set->log_capacity); b < b_end; b++)
	{
		struct E_(SetElement) *x = b->first;
		while(x) {
			if(!is_first) *s++ = comma, *s++ = space;
			else *s++ = space, is_first = 0;
			s[11] = '\0';
			/* Directly to the buffer;
			 might be a strict aliasing violation, (`C++` it is.) */
			PE_(to_string)(&x->data, (char (*)[12])s);
			for(i = 0; *s != '\0' && i < 12; s++, i++);
			/* No space to guarantee another element; terminate by ellipsis. */
			if((size_t)(s - string) > string_size
			   - 2 - 11 - ellipsis_end_len - 1) goto ellipsis;
			x = x->next;
		}
	}
end_buckets:
	if(!is_first) *s++ = space;
	*s++ = end;
	goto terminate;
ellipsis:
	memcpy(s, ellipsis_end, ellipsis_end_len), s += ellipsis_end_len;
terminate:
	*s++ = '\0';
	assert(s <= string + string_size);
	return string;
}

#endif /* print --> */

#ifdef SET_TEST /* <!-- test: need this file. */
#include "../test/TestSet.h" /** \include */
#endif /* test --> */

static void PE_(unused_coda)(void);
/** This silences unused function warnings from the pre-processor, but allows
 optimisation, (hopefully.)
 <http://stackoverflow.com/questions/43841780/silencing-unused-static-function-warnings-for-a-section-of-code> */
static void PE_(unused_set)(void) {
	E_(Set_)(0);
	E_(Set)(0);
	E_(SetClear)(0);
	E_(SetSize)(0);
	E_(SetReserve)(0, 0);
	E_(SetGet)(0, 0);
	E_(SetPut)(0, 0);
	E_(SetPolicyPut)(0, 0, 0);
	E_(SetRemove)(0, 0);
#ifdef SET_TO_STRING
	E_(SetToString)(0);
#endif
	PE_(unused_coda)();
}
static void PE_(unused_coda)(void) { PE_(unused_set)(); }

/* Un-define all macros. */
#undef SET_NAME
#undef SET_TYPE
/* Undocumented; allows nestled inclusion so long as: `CAT`, _etc_ conform to
 the definitions, and `E` is not used. */
#ifdef SET_SUBTYPE /* <!-- sub */
#undef SET_SUBTYPE
#else /* sub --><!-- !sub */
#undef CAT
#undef CAT_
#undef PCAT
#undef PCAT_
#endif /* !sub --> */
#undef SET_SIZE_MAX
#undef E
#undef PE
#undef E_
#undef PE_
#undef SET_NAME
#undef SET_TYPE
#undef SET_HASH
#undef SET_IS_EQUAL
#ifdef SET_TO_STRING /* <!-- string */
#undef SET_TO_STRING
#endif /* string --> */
#ifdef SET_PASS_POINTER /* <!-- !pointer */
#undef SET_PASS_POINTER
#endif /* !pointer --> */
#ifdef SET_NO_CACHE /* <!-- !cache */
#undef SET_NO_CACHE
#endif /* !cache --> */
#ifdef SET_HASH_TYPE /* <!-- hash type */
#undef SET_HASH_TYPE
#endif /* hash type --> */
#ifdef SET_TEST /* <!-- test */
#undef SET_TEST
#endif /* test --> */
