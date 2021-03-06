/**
 * cppcmb.hpp
 *
 * @author Peter Lenkefi
 * @date 2018-09-05
 * @description Generic parser combinators for C++17.
 */

#ifndef CPPCMB_HPP
#define CPPCMB_HPP

#include <functional>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cppcmb {

/**
 * Here are the combinator implementations and other type trait utilities.
 */
namespace detail {
	/**
	 * A custom optional wrapper to differentiate std::optional from nullables
	 * returned by a mapping failure.
	 */
	template <typename T>
	struct expected : private std::optional<T> {
		expected(std::optional<T> const&) = delete;
		expected(std::optional<T>&&) = delete;

		expected(expected const&) = default;
		expected(expected&&) = default;

		using std::optional<T>::optional;
		using std::optional<T>::operator=;
		using std::optional<T>::operator->;
		using std::optional<T>::operator*;
		using std::optional<T>::operator bool;
		using std::optional<T>::has_value;
		using std::optional<T>::value;
		using std::optional<T>::value_or;
	};

	/**
	 * Helper function to make the failable type.
	 */
	template <typename T>
	constexpr auto make_expected(T&& val) {
		return expected<std::decay_t<T>>(std::forward<T>(val));
	}

	template <typename T, typename... Args> 
	constexpr auto make_expected(Args&&... args) {
		return expected<T>(std::in_place, std::forward<Args>(args)...);
	}

	template <typename T, typename U, typename... Args> 
	constexpr auto make_expected(std::initializer_list<U> il, Args&&... args) {
		return expected<T>(std::in_place, il, std::forward<Args>(args)...);
	}

	/**
	 * A helper functionality that wraps any value into a tuple if it's not
	 * already a tuple.
	 */
	template <typename T>
	struct as_tuple_impl {
		template <typename TFwd>
		static constexpr auto pass(TFwd&& arg) {
			return std::make_tuple(std::forward<TFwd>(arg));
		}
	};

	template <typename... Ts>
	struct as_tuple_impl<std::tuple<Ts...>> {
		template <typename TFwd>
		static constexpr auto pass(TFwd&& arg) {
			return arg;
		}
	};

	template <typename TFwd>
	static constexpr auto as_tuple(TFwd&& arg) {
		return as_tuple_impl<std::decay_t<TFwd>>::pass(std::forward<TFwd>(arg));
	}

	/**
	 * A helper functionality that unwraps a tuple if it can.
	 */
	template <typename T>
	struct unwrap_tuple_impl {
		template <typename TFwd>
		static constexpr auto pass(TFwd&& arg) {
			return arg;
		}
	};

	template <typename T>
	struct unwrap_tuple_impl<std::tuple<T>> {
		template <typename TFwd>
		static constexpr auto pass(TFwd&& arg) {
			return std::get<0>(std::forward<TFwd>(arg));
		}
	};

	template <typename TFwd>
	static constexpr auto unwrap_tuple(TFwd&& arg) {
		return unwrap_tuple_impl<std::decay_t<TFwd>>::pass(
			std::forward<TFwd>(arg));
	}

	/**
	 * General result type.
	 */
	template <typename TokenIterator, typename... Data>
	using result_type = std::optional<std::pair<decltype(
		unwrap_tuple(std::declval<std::tuple<Data...>>())), TokenIterator
	>>;

	/**
	 * We need to forward-declare functionality for the subscript operator.
	 */
	template <typename Combinator, typename Mapper>
	static constexpr auto make_subscript_map(Combinator&&, Mapper&&);

	/**
	 * Wraps a free function into a functor type. Cannot be lambda or 
	 * std::function. If a TokenIterator type is provided, it becomes a
	 * combinator function.
	 * Every combinator function must be wrapped in this.
	 */
	template <auto, typename...>
	struct fn_wrap;

	template <auto Callable>
	struct fn_wrap<Callable> {
		constexpr fn_wrap() = default;

		template <typename... Ts>
		constexpr auto operator()(Ts&&... args) const {
			return Callable(std::forward<Ts>(args)...);
		}
	};

	template <auto Callable, typename TokenIterator>
	struct fn_wrap<Callable, TokenIterator> {
		// Define common types
		using iterator_type = TokenIterator;
		using callable_type = std::decay_t<decltype(Callable)>;
		using return_type = std::invoke_result_t<callable_type, TokenIterator>;
		using pair_type = typename return_type::value_type;
		using data_type = typename pair_type::first_type;

		static_assert(
			std::is_same_v<typename pair_type::second_type, TokenIterator>,
			"Iterator type mismatch in function wrapper!"
		);

		constexpr fn_wrap() = default;

		template <typename... Ts>
		constexpr auto operator()(TokenIterator it) const {
			return Callable(it);
		}

		template <typename Mapper>
		constexpr auto operator[](Mapper&& m) const {
			return make_subscript_map(*this, std::forward<Mapper>(m));
		}
	};

	/**
	 * Creates a result value.
	 * Result values are in the form of ((data...), position)?.
	 */
	template <typename Data, typename TokenIterator>
	static constexpr auto make_result(Data&& data, TokenIterator it) {
		return std::make_optional(std::make_pair(
			std::forward<Data>(data), it
		));
	}

	/**
	 * The simplest combinator that succeeds with an empty result.
	 */
	template <typename TokenIterator>
	static constexpr auto cmb_succ_fn(TokenIterator it) {
		return make_result(std::make_tuple(), it);
	}

	template <typename TokenIterator>
	using cmb_succ = fn_wrap<cmb_succ_fn<TokenIterator>, TokenIterator>;

	/**
	 * A combinator that returns the current token and advances the position by
	 * one.
	 */
	template <typename TokenIterator>
	static constexpr auto cmb_one_fn(TokenIterator it) {
		return make_result(*it, std::next(it));
	}

	template <typename TokenIterator>
	using cmb_one = fn_wrap<cmb_one_fn<TokenIterator>, TokenIterator>;

	/**
	 * Wraps another combinator so that it's return data becomes optional. This
	 * combinator therefore always succeeds.
	 */
	template <typename TokenIterator, typename Combinator>
	static constexpr auto cmb_opt_fn(TokenIterator it) {
		auto res = Combinator()(it);
		if (!res) {
			// We didn't advance position
			using data_type = typename Combinator::data_type;
			return make_result(std::optional<data_type>(), it);
		}
		return make_result(std::make_optional(res->first), res->second);
	}

	template <typename TokenIterator, typename Combinator>
	using cmb_opt = fn_wrap<
		cmb_opt_fn<TokenIterator, Combinator>, TokenIterator
	>;

	/**
	 * Applies combinators in a sequence and concatenates the results if all of
	 * them succeeds. If one fails, the whole sequence fails.
	 */
	template <typename TokenIterator>
	static constexpr auto cmb_seq_fn(TokenIterator it) {
		// End of sequence, just succeed
		return make_result(std::make_tuple(), it);
	}

	template <typename TokenIterator, typename First, typename... Rest>
	static constexpr auto cmb_seq_fn(TokenIterator it) {
		auto first = First()(it);
		using result_type = decltype(make_result(
			unwrap_tuple(std::tuple_cat(
				as_tuple(first->first),
				as_tuple(cmb_seq_fn<TokenIterator, Rest...>(it)->first)
			)),
			it
		));
		if (!first) {
			return result_type();
		}
		auto rest = cmb_seq_fn<TokenIterator, Rest...>(first->second);
		if (!rest) {
			return result_type();
		}
		return make_result(
			unwrap_tuple(std::tuple_cat(
				as_tuple(std::move(first->first)),
				as_tuple(std::move(rest->first))
			)),
			rest->second
		);
	}

	template <typename TokenIterator, typename... Combinators>
	using cmb_seq = fn_wrap<
		cmb_seq_fn<TokenIterator, Combinators...>, TokenIterator
	>;

	/**
	 * Applies the combinators and returns with the first succeeding one. If
	 * none of them succeeds, the combinator fails.
	 */
	template <typename TokenIterator, typename ResultData>
	static constexpr auto cmb_alt_fn(TokenIterator) {
		// End of alternatives, fail
		return std::optional<std::pair<ResultData, TokenIterator>>();
	}

	template <typename TokenIterator, typename ResultData,
		typename First, typename... Rest>
	static constexpr auto cmb_alt_fn(TokenIterator it) {
		auto first = First()(it);
		if (first) {
			return first;
		}
		return cmb_alt_fn<TokenIterator, ResultData, Rest...>(it);
	}

	template <typename TokenIterator, typename First, typename... Rest>
	using cmb_alt = fn_wrap<
		cmb_alt_fn<
			TokenIterator,
			typename First::data_type,
			First, Rest...
		>,
		TokenIterator
	>;

	/**
	 * Repeatedly applies a combinator while it succeeds. Stops on faliure.
	 * Collects result into a collection. Always succeeds.
	 */
	template <typename TokenIterator, 
		template <typename...> typename Collection, typename Combinator>
	static constexpr auto cmb_rep_fn(TokenIterator it) {
		using element_type = typename Combinator::data_type;
		Collection<element_type> result;
		auto out_it = std::back_inserter(result);
		while (true) {
			auto res = Combinator()(it);
			if (!res) {
				return make_result(std::move(result), it);
			}
			// Advance
			*out_it++ = std::move(res->first);
			it = res->second;
		}
	}

	template <typename TokenIterator, 
		template <typename...> typename Collection, typename Combinator>
	using cmb_rep = fn_wrap<
		cmb_rep_fn<TokenIterator, Collection, Combinator>, TokenIterator
	>;

	/**
	 * Repeatedly applies a combinator while it succeeds. Stops on faliure.
	 * Collects result into a collection. Succeeds if it collected at least one
	 * element.
	 */
	template <typename TokenIterator, 
		template <typename...> typename Collection, typename Combinator>
	static constexpr auto cmb_rep1_fn(TokenIterator it) {
		auto res = cmb_rep_fn<TokenIterator, Collection, Combinator>(it);
		using res_type = decltype(res);
		if (res->first.empty()) {
			// Empty, fail
			return res_type();
		}
		return res;
	}

	template <typename TokenIterator, 
		template <typename...> typename Collection, typename Combinator>
	using cmb_rep1 = fn_wrap<
		cmb_rep1_fn<TokenIterator, Collection, Combinator>, TokenIterator
	>;

	/**
	 * A wrapper for the map combinator to detect transformations that can fail.
	 */
	template <typename RetTy,
		typename TokenIterator, typename Combinator, typename Mapper>
	struct cmb_map_impl {
		constexpr cmb_map_impl() = default;

		static constexpr auto pass(TokenIterator it) {
			auto res = Combinator()(it);
			if (res) {
				return make_result(
					unwrap_tuple(std::apply(Mapper(), as_tuple(res->first))),
					res->second
				);
			}
			// XXX(LPeter1997): Simplify?
			using result_type = decltype(make_result(
				unwrap_tuple(std::apply(Mapper(), as_tuple(res->first))),
				it
			));
			return result_type();
		}
	};

	template <typename TokenIterator, typename Combinator, typename Mapper,
		typename T>
	struct cmb_map_impl<expected<T>, TokenIterator, Combinator, Mapper> {
		constexpr cmb_map_impl() = default;

		static constexpr auto pass(TokenIterator it) {
			auto res = Combinator()(it);
			// XXX(LPeter1997): Simplify?
			using result_type = decltype(make_result(
				unwrap_tuple(*std::apply(Mapper(), as_tuple(res->first))),
				it
			));
			if (res) {
				auto transformed = std::apply(Mapper(), as_tuple(res->first));
				if (transformed) {
					return make_result(
						unwrap_tuple(std::move(*transformed)),
						res->second
					);
				}
			}
			return result_type();
		}
	};

	/**
	 * Applies a combinator. If it succeeded, the result data is applied to a
	 * transformation function. If the transformation can fail, then it's also
	 * considered when returning (on transformation failure the combinator
	 * fails).
	 */
	template <typename TokenIterator, typename Combinator, typename Mapper>
	static constexpr auto cmb_map_fn(TokenIterator it) {
		using combinator_result = decltype(Combinator()(it));
		using transform_result = decltype(std::apply(
			Mapper(),
			as_tuple(std::declval<combinator_result>()->first)
		));
		return cmb_map_impl<std::decay_t<transform_result>,
			TokenIterator, Combinator, Mapper>::pass(it);
	}

	template <typename TokenIterator, typename Combinator, typename Mapper>
	using cmb_map = fn_wrap<
		cmb_map_fn<TokenIterator, Combinator, Mapper>, TokenIterator
	>;

	/**
	 * Filters the result. If the predicate is true, it succeeds, fails
	 * otherwise.
	 */
	template <typename Predicate>
	struct filter_impl {
		constexpr filter_impl() = default;

		template <typename... Ts>
		constexpr auto operator()(Ts&&... args) const {
			using res_type = decltype(make_expected(
				unwrap_tuple(std::make_tuple(std::forward<Ts>(args)...))
			));
			if (Predicate()(args...)) {
				return make_expected(
					unwrap_tuple(std::make_tuple(std::forward<Ts>(args)...))
				);
			}
			return res_type();
		}
	};

	/**
	 * Selector function that returns some elements of the tuple.
	 */
	template <std::size_t... Indicies>
	struct select_impl {
		constexpr select_impl() = default;

		template <typename... Ts>
		constexpr auto operator()(Ts&&... args) const {
			return unwrap_tuple(std::make_tuple(std::get<Indicies>(
				std::make_tuple(std::forward<Ts>(args)...)
			)...));
		}
	};

	/**
	 * Fold left function.
	 * f(f(f(f(f(..., f), g), h), i), j)
	 */
	template <typename Folder>
	struct foldl_impl {
		constexpr foldl_impl() = default;

		template <typename Init, typename Rest>
		constexpr auto operator()(Init&& first, Rest&& rest) const {
			for (auto it = std::cbegin(rest); it != std::cend(rest); ++it) {
				first = Folder()(std::move(first), *it);
			}
			return first;
		}
	};

	/**
	 * Fold right function.
	 * f(a, f(b, f(c, f(d, f(e, ...)))))
	 */
	template <typename Folder>
	struct foldr_impl {
		constexpr foldr_impl() = default;

		template <typename Init, typename Rest>
		constexpr auto operator()(Init&& rest, Rest&& first) const {
			for (auto it = std::crbegin(rest); it != std::crend(rest); ++it) {
				first = Folder()(*it, std::move(first));
			}
			return first;
		}
	};

	/**
	 * Mechanism for matching combinators. This is the reason we need all
	 * combinators as fn_wrap-s.
	 */
	template <typename T>
	struct is_combinator : std::false_type {};

	template <auto Callable, typename TokenIterator>
	struct is_combinator<fn_wrap<Callable, TokenIterator>> : std::true_type {};

	template <typename T>
	static constexpr bool is_combinator_v = is_combinator<T>::value;

	/**
	 * SFINAE test for combinators.
	 */
	template <typename T>
	struct enable_if_combinator : public std::enable_if<
		is_combinator_v<std::decay_t<T>>
	> {};

	template <typename T>
	using enable_if_combinator_t = typename enable_if_combinator<T>::type;
}

/**
 * A module interface for a template-style grammar definition.
 */
template <typename TokenIterator>
struct combinator_types {
	template <typename T>
	using expected = detail::expected<T>;

	template <typename T>
	constexpr auto make_expected(T&& val) {
		return detail::make_expected(std::forward<T>(val));
	}

	template <typename T, typename... Args> 
	constexpr auto make_expected(Args&&... args) {
		return detail::make_expected<T>(std::forward<Args>(args)...);
	}

	template <typename T, typename U, typename... Args> 
	constexpr auto make_expected(std::initializer_list<U> il, Args&&... args) {
		return detail::make_expected<T>(il, std::forward<Args>(args)...);
	}

	template <typename... Data>
	using result_type = detail::result_type<TokenIterator, Data...>;

	using succ = detail::cmb_succ<TokenIterator>;

	using one = detail::cmb_one<TokenIterator>;

	template <typename Combinator>
	using opt = detail::cmb_opt<TokenIterator, Combinator>;

	template <typename... Combinators>
	using seq = detail::cmb_seq<TokenIterator, Combinators...>;

	template <typename First, typename... Rest>
	using alt = detail::cmb_alt<TokenIterator, First, Rest...>;

	template <template <typename...> typename Collection, typename Combinator>
	using rep = detail::cmb_rep<TokenIterator, Collection, Combinator>;

	template <template <typename...> typename Collection, typename Combinator>
	using rep1 = detail::cmb_rep1<TokenIterator, Collection, Combinator>;

	template <typename Combinator, typename Mapper>
	using map = detail::cmb_map<TokenIterator, Combinator, Mapper>;

	template <auto Fn>
	using wrap = detail::fn_wrap<Fn, TokenIterator>;

	template <auto Fn>
	using fn = detail::fn_wrap<Fn>;

	template <typename Predicate>
	using filter = detail::filter_impl<Predicate>;

	template <std::size_t... Indicies>
	using select = detail::select_impl<Indicies...>;

	template <typename Folder>
	using foldl = detail::foldl_impl<Folder>;

	template <typename Folder>
	using foldr = detail::foldr_impl<Folder>;
};

template <typename TokenIterator>
struct combinator_values {
	using types = combinator_types<TokenIterator>;

	template <typename T>
	using expected = typename types::template expected<T>;

	template <typename T>
	constexpr auto make_expected(T&& val) {
		return detail::make_expected(std::forward<T>(val));
	}

	template <typename T, typename... Args> 
	constexpr auto make_expected(Args&&... args) {
		return detail::make_expected<T>(std::forward<Args>(args)...);
	}

	template <typename T, typename U, typename... Args> 
	constexpr auto make_expected(std::initializer_list<U> il, Args&&... args) {
		return detail::make_expected<T>(il, std::forward<Args>(args)...);
	}

	template <typename... Data>
	using result_type = typename types::template result_type<Data...>;

	static constexpr auto succ = typename types::succ();

	static constexpr auto one = typename types::one();

	template <typename Combinator>
	static constexpr auto opt(Combinator&&) {
		return typename types::template opt<std::decay_t<Combinator>>();
	}

	template <typename... Combinators>
	static constexpr auto seq(Combinators&&...) {
		return typename types::template seq<std::decay_t<Combinators>...>();
	}

	template <typename First, typename... Rest>
	static constexpr auto alt(First&&, Rest&&...) {
		return typename types::template alt<
			std::decay_t<First>, std::decay_t<Rest>...>();
	}

	template <template <typename...> typename Collection, typename Combinator>
	static constexpr auto rep(Combinator&&) {
		return typename types::template rep<Collection,
			std::decay_t<Combinator>>();
	}

	template <template <typename...> typename Collection, typename Combinator>
	static constexpr auto rep1(Combinator&&) {
		return typename types::template rep1<Collection,
			std::decay_t<Combinator>>();
	}

	template <typename Combinator, typename Mapper>
	static constexpr auto map(Combinator&&, Mapper&&) {
		return typename types::template map<std::decay_t<Combinator>,
			std::decay_t<Mapper>>();
	}

	template <auto Fn>
	static constexpr auto wrap = typename types::template wrap<Fn>();

	template <auto Fn>
	static constexpr auto fn = typename types::template fn<Fn>();

	template <typename Predicate>
	static constexpr auto filter(Predicate&&) {
		return typename types::template filter<std::decay_t<Predicate>>();
	}

	template <std::size_t... Indicies>
	static constexpr auto select = 
		typename types::template select<Indicies...>();

	template <typename Folder>
	static constexpr auto foldl(Folder&&) {
		return typename types::template foldl<std::decay_t<Folder>>();
	}

	template <typename Folder>
	static constexpr auto foldr(Folder&&) {
		return typename types::template foldr<std::decay_t<Folder>>();
	}
};

/**
 * We can implement the subscript hack here.
 */
namespace detail {
	template <typename Combinator, typename Mapper>
	static constexpr auto make_subscript_map(Combinator&& c, Mapper&& m) {
		using combinator_t = std::decay_t<Combinator>;
		using iter_type = typename combinator_t::iterator_type;

		return combinator_values<iter_type>::map(
			std::forward<Combinator>(c), std::forward<Mapper>(m));
	}
}

} /* namespace cppcmb */

/**
 * Operators for nicer syntax.
 */

/**
 * Optional.
 */
template <typename Combinator, 
	typename = cppcmb::detail::enable_if_combinator_t<Combinator>>
static constexpr auto operator~(Combinator&& c) {
	using combinator_t = std::decay_t<Combinator>;

	using iter_type = typename combinator_t::iterator_type;

	return cppcmb::combinator_values<iter_type>::opt(
		std::forward<Combinator>(c)
	);
}

/**
 * Sequencing.
 */
template <typename Left, typename Right,
	typename = cppcmb::detail::enable_if_combinator_t<Left>,
	typename = cppcmb::detail::enable_if_combinator_t<Right>>
static constexpr auto operator&(Left&& l, Right&& r) {
	using left_t = std::decay_t<Left>;
	using right_t = std::decay_t<Right>;

	using iter_type = typename left_t::iterator_type;

	static_assert(std::is_same_v<
		iter_type, typename right_t::iterator_type>,
		"Sequenced iterator types must match!");

	return cppcmb::combinator_values<iter_type>::seq(
		std::forward<Left>(l), std::forward<Right>(r)
	);
}

/**
 * Alternatives.
 */
template <typename Left, typename Right,
	typename = cppcmb::detail::enable_if_combinator_t<Left>,
	typename = cppcmb::detail::enable_if_combinator_t<Right>>
static constexpr auto operator|(Left&& l, Right&& r) {
	using left_t = std::decay_t<Left>;
	using right_t = std::decay_t<Right>;

	using iter_type = typename left_t::iterator_type;

	static_assert(std::is_same_v<
		iter_type, typename right_t::iterator_type>,
		"Alternate iterator types must match!");

	return cppcmb::combinator_values<iter_type>::alt(
		std::forward<Left>(l), std::forward<Right>(r)
	);
}

#endif /* CPPCMB_HPP */
