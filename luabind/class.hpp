// Copyright (c) 2003 Daniel Wallin and Arvid Norberg

// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
// ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
// TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
// SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
// ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.


#ifndef LUABIND_CLASS_HPP_INCLUDED
#define LUABIND_CLASS_HPP_INCLUDED

/*
	ISSUES:
	------------------------------------------------------

	* solved for member functions, not application operator *
	if we have a base class that defines a function a derived class must be able to
	override that function (not just overload). Right now we just add the other overload
	to the overloads list and will probably get an ambiguity. If we want to support this
	each method_rep must include a vector of type_info pointers for each parameter.
	Operators do not have this problem, since operators always have to have
	it's own type as one of the arguments, no ambiguity can occur. Application
	operator, on the other hand, would have this problem.
	Properties cannot be overloaded, so they should always be overridden.
	If this is to work for application operator, we really need to specify if an application
	operator is const or not.

	If one class registers two functions with the same name and the same
	signature, there's currently no error. The last registered function will
	be the one that's used.
	How do we know which class registered the function? If the function was
	defined by the base class, it is a legal operation, to override it.
	we cannot look at the pointer offset, since it always will be zero for one of the bases.



	TODO:
	------------------------------------------------------

	add access to the keys on iterators

	finish smart pointer support
		* the adopt policy should not be able to adopt pointers to held_types. This
		must be prohibited.
		* name_of_type must recognize holder_types and not return "custom"
		* maybe it's better to avoid LUABIND_TYPEID() etc. in user code. get_const_holder
		* document it

	document the new yield-policy

	chache operators and finalizers in the class_rep. For lua classes
	we currently do a lookup each time we need to know if a lua class
	has a finalizer or an operator.

	instead of registering the name of free functions and make a lookup in
	the register to see if we are overloading, look in the actual namespace
	where we are registering the function to see if there already is a function
	with the same name there, and in that case, add an overload to it.

	static functions, this could be implemented by letting classes contain
	other declarations (classes or functions)

	document custom policies, custom converters

	store the instance object for policies.

	support the __concat metamethod. This is a bit tricky, since it cannot be
	treated as a normal operator. It is a binary operator but we want to use the
	__tostring implementation for both arguments.
	
*/

#include <luabind/config.hpp>

#include <string>
#include <map>
#include <vector>
#include <cassert>

#include <boost/static_assert.hpp>
#include <boost/type_traits.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/preprocessor/repetition/enum_params.hpp>
#include <boost/preprocessor/repetition/enum_params_with_a_default.hpp>
#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/type_traits/is_same.hpp>
#include <boost/mpl/list.hpp>
#include <boost/mpl/apply.hpp>
#include <boost/mpl/lambda.hpp>
#include <boost/mpl/logical.hpp>
#include <boost/mpl/find_if.hpp>
#include <boost/mpl/apply_if.hpp>
#include <boost/mpl/logical.hpp>

#include <luabind/config.hpp>
#include <luabind/scope.hpp>
#include <luabind/detail/constructor.hpp>
#include <luabind/detail/call.hpp>
#include <luabind/detail/signature_match.hpp>
#include <luabind/detail/primitives.hpp>
#include <luabind/detail/property.hpp>
#include <luabind/detail/typetraits.hpp>
#include <luabind/detail/class_rep.hpp>
#include <luabind/detail/method_rep.hpp>
#include <luabind/detail/construct_rep.hpp>
#include <luabind/detail/object_rep.hpp>
#include <luabind/detail/operators.hpp>
#include <luabind/detail/calc_arity.hpp>
#include <luabind/detail/call_member.hpp>
#include <luabind/detail/enum_maker.hpp>
#include <luabind/detail/get_signature.hpp>
#include <luabind/detail/implicit_cast.hpp>
#include <luabind/detail/operator_id.hpp>

namespace luabind
{	
	namespace detail
	{
		struct unspecified {};
	}

	template<class T, class X1 = detail::unspecified, class X2 = detail::unspecified, class X3 = detail::unspecified>
	struct class_;

	inline LUABIND_TYPE_INFO get_const_holder(...)
	{
		return LUABIND_INVALID_TYPE_INFO;
	}

	namespace detail
	{
		template<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_BASES, class A)>
		double is_bases_helper(const bases<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_BASES, A)>&);

#ifndef BOOST_MSVC
		template<class T>
		char is_bases_helper(const T&);
#else
		char is_bases_helper(...);
#endif

		template<class T>
		struct is_bases
		{
			static const T& t;

			BOOST_STATIC_CONSTANT(bool, value = sizeof(is_bases_helper(t)) == sizeof(double));
			typedef boost::mpl::bool_<value> type;
			BOOST_MPL_AUX_LAMBDA_SUPPORT(1,is_bases,(T))
		};

		double is_not_unspecified_helper(const unspecified*);
		char is_not_unspecified_helper(...);

		template<class T>
		struct is_not_unspecified
		{
			BOOST_STATIC_CONSTANT(bool, value = sizeof(is_not_unspecified_helper(static_cast<T*>(0))) == sizeof(char));
			typedef boost::mpl::bool_<value> type;
			BOOST_MPL_AUX_LAMBDA_SUPPORT(1,is_not_unspecified,(T))
		};

		template<class Predicate>
		struct get_predicate
		{
			typedef typename boost::mpl::and_<
						Predicate
					,	is_not_unspecified<boost::mpl::_1>
					> type;
		};

		template<class Parameters, class Predicate, class DefaultValue>
		struct extract_parameter
		{
			typedef typename get_predicate<Predicate>::type pred;
			typedef typename boost::mpl::find_if<Parameters, pred>::type iterator;
			typedef typename boost::mpl::apply_if<boost::is_same<iterator, typename boost::mpl::end<Parameters>::type>
				, boost::mpl::identity<DefaultValue>
				, iterator
			>::type type;
		};

		int function_dispatcher(lua_State* L);

		// this should know about the smart pointer type.
		// and should really do:
		// get_pointer(*static_cast<SmartPointer*>(obj_ptr))
		// to extract the held pointer.
		template<class HeldType, class T, class F, class Policies>
		struct function_callback_non_null : Policies
		{
			function_callback_non_null(F f_): f(f_) {}
			inline int operator()(lua_State* L, void* obj_ptr)
			{
				HeldType& held_obj = *static_cast<HeldType*>(obj_ptr);

				T* ptr = static_cast<T*>(luabind::get_pointer(held_obj));

				return call(f, ptr, L, static_cast<Policies*>(this));
			}
			F f;
		};

		template<class T, class F, class Policies>
		struct function_callback_null_type : Policies
		{
			function_callback_null_type(F f_): f(f_) {}
			inline int operator()(lua_State* L, void* obj_ptr)
			{
//				std::cout << "HeldType: null_type\n";
				T* ptr = static_cast<T*>(obj_ptr);
				return call(f, ptr, L, static_cast<Policies*>(this));
			}
			F f;
		};

		template<class HeldType, class T, class F, class Policies>
		struct function_callback_s
		{
			typedef typename
				boost::mpl::if_<boost::is_same<HeldType,detail::null_type>
					, function_callback_null_type<T,F,Policies>
					, function_callback_non_null<HeldType,T,F,Policies>
			>::type type;
		};

		template<class T, class F, class Policies>
		struct match_function_callback_s
		{
			static inline int apply(lua_State* L)
			{
				object_rep* obj = static_cast<object_rep*>(lua_touserdata(L, 1));
				F fptr = 0;
				return match(fptr, L, obj->flags() & object_rep::constant, static_cast<Policies*>(0));
			}
		};

		// prints the types of the values on the stack, in the
		// range [start_index, lua_gettop()]

		std::string stack_content_by_name(lua_State* L, int start_index);
	
		struct create_class
		{
			static int stage1(lua_State* L);
			static int stage2(lua_State* L);
		};

		template<class Type>
		struct register_wrapped_type
		{
			template<class Signature, class Policies>
			static void apply(detail::construct_rep::overload_t& o, const Signature*, const Policies*)
			{
				o.set_wrapped_constructor(
					&detail::construct_wrapped_class<Type, Policies, Signature>::apply
				); 
			}
		};

		template<>
		struct register_wrapped_type<detail::null_type>
		{
			template<class Signature, class Policies>
			static void apply(detail::construct_rep::overload_t&, const Signature*, const Policies*) {}
		};


		// if the class is held by a smart pointer, we need to be able to
		// implicitly dereference the pointer when needed.

		template<class UnderlyingT, class HeldT>
		struct extract_underlying_type
		{
			static void* extract(void* ptr)
			{
				HeldT& held_obj = *reinterpret_cast<HeldT*>(ptr);
				UnderlyingT* underlying_ptr = static_cast<UnderlyingT*>(get_pointer(held_obj));
				return underlying_ptr;
			}
		};


		template<class HeldType>
		struct internal_held_type_extractor
		{
			typedef void*(*extractor_fun)(void*);

			template<class T>
			static extractor_fun apply(detail::type<T>)
			{
				return detail::extract_underlying_type<T, HeldType>::extract;
			}
		};

		template<>
		struct internal_held_type_extractor<detail::null_type>
		{
			typedef void*(*extractor_fun)(void*);

			template<class T>
			static extractor_fun apply(detail::type<T>)
			{
				return 0;
			}
		};


		// this is simply a selector that returns the type_info
		// of the held type, or invalid_type_info if we don't have
		// a held_type
		template<class HeldType>
		struct internal_held_type
		{
			static LUABIND_TYPE_INFO apply()
			{
				return LUABIND_TYPEID(HeldType);
			}
		};

		template<>
		struct internal_held_type<detail::null_type>
		{
			static LUABIND_TYPE_INFO apply()
			{
				return LUABIND_INVALID_TYPE_INFO;
			}
		};


		// this is the actual held_type constructor
		template<class HeldType, class T>
		struct internal_construct_held_type
		{
			static void apply(void* target, void* raw_pointer)
			{
				new(target) HeldType(static_cast<T*>(raw_pointer));
			}
		};

		// the followinf two functions are the ones that returns
		// a pointer to a held_type_constructor, or 0 if there
		// is no held_type
		template<class HeldType>
		struct internal_held_type_constructor
		{
			typedef void(*constructor)(void*,void*);
			template<class T>
			static constructor apply(detail::type<T>)
			{
				return &internal_construct_held_type<HeldType, T>::apply;
			}
		};

		template<>
		struct internal_held_type_constructor<detail::null_type>
		{
			typedef void(*constructor)(void*,void*);
			template<class T>
			static constructor apply(detail::type<T>)
			{
				return 0;
			}
		};


		// this is a selector that returns the size of the held_type
		// or 0 if we don't have a held_type
		template <class HeldType>
		struct internal_held_type_size
		{
			static int apply() { return sizeof(HeldType); }
		};

		template <>
		struct internal_held_type_size<detail::null_type>
		{
			static int apply() {	return 0; }
		};


		// if we have a held type, return the destructor to it
		// note the difference. The held_type should only be destructed (not deleted)
		// since it's constructed in the lua userdata
		template<class HeldType>
		struct internal_held_type_destructor
		{
			typedef void(*destructor_t)(void*);
			template<class T>
			static destructor_t apply(detail::type<T>)
			{
				return &detail::destruct_only_s<HeldType>::apply;
			}
		};

		// if we don't have a held type, return the destructor of the raw type
		template<>
		struct internal_held_type_destructor<detail::null_type>
		{
			typedef void(*destructor_t)(void*);
			template<class T>
			static destructor_t apply(detail::type<T>)
			{
				return &detail::delete_s<T>::apply;
			}
		};

		template<class T>
		struct get_holder_alignment
		{
			static int apply()
			{
				return boost::alignment_of<T>::value;
			}
		};

		template<>
		struct get_holder_alignment<detail::null_type>
		{
			static int apply()
			{
				return 0;
			}
		};


	} // detail














	struct class_base: detail::scoped_object
	{
	protected:

		struct base_desc
		{
			LUABIND_TYPE_INFO type;
			int ptr_offset;
		};

	private:

#ifndef NDEBUG
		bool m_cloned;
#endif

		const char* m_name;

		std::map<const char*, detail::method_rep, detail::ltstr> m_methods;

		// datamembers, some members may be readonly, and
		// only have a getter function
		std::map<const char*, detail::class_rep::callback, detail::ltstr> m_getters;
		std::map<const char*, detail::class_rep::callback, detail::ltstr> m_setters;

		// the operators in lua
		std::vector<detail::class_rep::operator_callback> m_operators[detail::number_of_operators]; 
		std::map<const char*, int, detail::ltstr> m_static_constants;

		std::vector<base_desc> m_bases;
		detail::construct_rep m_constructor;

		void(*m_destructor)(void*);
		void*(*m_extractor)(void*);
		void(*m_construct_held_type)(void*, void*);
		int m_held_type_size;
		int m_held_type_alignment;

		LUABIND_TYPE_INFO m_type;
		LUABIND_TYPE_INFO m_held_type;
		LUABIND_TYPE_INFO m_const_holder_type;

#ifndef LUABIND_DONT_COPY_STRINGS
		// the maps that contains char pointers points into
		// this vector of strings. 
		std::vector<char*> m_strings;
#endif

	public:

		// public 'cause of enum_maker, FIX
		void add_static_constant(const char* name, int val)
		{
			m_static_constants[name] = val;
		}

	protected:

		void set_type(LUABIND_TYPE_INFO t) { m_type = t; }
		void set_held_type(LUABIND_TYPE_INFO t) { m_held_type = t; }

		void set_const_holder_type(LUABIND_TYPE_INFO t)
		{
			m_const_holder_type = t;	
		}

		void set_extractor(void*(*f)(void*)) { m_extractor = f; }
		void set_held_type_constructor(void(*f)(void*,void*)) { m_construct_held_type = f; }
		void set_destructor(void(*f)(void*)) { m_destructor = f; }
		void set_held_type_size(int s) { m_held_type_size = s; }
		void set_held_type_alignment(int n) { m_held_type_alignment = n; }

		inline void add_getter(const char* name, const boost::function2<int, lua_State*, int>& g)
		{
			detail::class_rep::callback c;
			c.func = g;
			c.pointer_offset = 0;
#ifndef LUABIND_DONT_COPY_STRINGS
			m_strings.push_back(detail::dup_string(name));
			m_getters[m_strings.back()] = c;
#else
			m_getters[name] = c;
#endif
		}

		inline void add_setter(const char* name, const boost::function2<int, lua_State*, int>& s)
		{
			detail::class_rep::callback c;
			c.func = s;
			c.pointer_offset = 0;
#ifndef LUABIND_DONT_COPY_STRINGS
			m_strings.push_back(detail::dup_string(name));
			m_setters[m_strings.back()] = c;
#else
			m_setters[name] = c;
#endif
		}

		void add_base(const base_desc& b)
		{
			m_bases.push_back(b);
		}

	public:

		void add_constructor(const detail::construct_rep::overload_t& o)
		{
			m_constructor.overloads.push_back(o);
		}

		void add_method(const char* name, const detail::overload_rep& o)
		{
#ifdef LUABIND_DONT_COPY_STRINGS
			detail::method_rep& method = m_methods[name];
			method.name = name;
#else
			m_strings.push_back(detail::dup_string(name));
			detail::method_rep& method = m_methods[m_strings.back()];
			method.name = m_strings.back();
#endif
			method.add_overload(o);
			method.crep = 0;
		}

#ifndef LUABIND_NO_ERROR_CHECKING
		inline void add_operator(int op_id,  int(*func)(lua_State*), int(*matcher)(lua_State*), void(*sig)(lua_State*, std::string&), int arity)
#else
		inline void add_operator(int op_id,  int(*func)(lua_State*), int(*matcher)(lua_State*), int arity)
#endif
		{
			detail::class_rep::operator_callback o;
			o.set_fun(func);
			o.set_match_fun(matcher);
			o.set_arity(arity);

#ifndef LUABIND_NO_ERROR_CHECKING

			o.set_sig_fun(sig);

#endif
			m_operators[op_id].push_back(o);
		}







		const char* name() const { return m_name; }

		class_base(const char* name)
		{
#ifndef LUABIND_DONT_COPY_STRINGS
			m_strings.push_back(detail::dup_string(name));
			m_name = m_strings.back();
#else
			m_name = name;
#endif

#ifndef NDEBUG
			m_cloned = false;
#endif
		}

		virtual ~class_base()
		{
// if we are copying strings, we have to destroy them too
#ifndef LUABIND_DONT_COPY_STRINGS
			for (std::vector<char*>::iterator i = m_strings.begin(); i != m_strings.end(); ++i)
				delete[] *i;
#endif
		}

		// pushes the class_rep on the lua stack
		virtual void commit(lua_State* L)
		{
			assert(!m_cloned && "class already commited");
				  
			detail::getref(L, scope_stack::top(L));
			lua_pushstring(L, m_name);

			detail::class_rep* crep;

			detail::class_registry* r = detail::class_registry::get_registry(L);
			// create a class_rep structure for this class.
			// allocate it within lua to let lua collect it on
			// lua_close(). This is better than allocating it
			// as a static, since it will then be destructed
			// when the program exits instead.
			// warning: we assume that lua will not
			// move the userdata memory.
			lua_newuserdata(L, sizeof(detail::class_rep));
			crep = reinterpret_cast<detail::class_rep*>(lua_touserdata(L, -1));
			
			new(crep) detail::class_rep(m_type, m_name, L, m_destructor, m_held_type, m_const_holder_type, m_extractor, m_construct_held_type, m_held_type_size, m_held_type_alignment);

			// register this new type in the class registry
			r->add_class(m_type, crep);
			if (!(LUABIND_TYPE_INFO_EQUAL(m_held_type, LUABIND_INVALID_TYPE_INFO)))
			{
				// if we have a held type
				// we have to register it in the class-table
				// but only for the base class, if it already
				// exists, we don't have to register it
				detail::class_rep* c = r->find_class(m_held_type);
				if (c == 0)
				{
					r->add_class(m_held_type, crep);
					r->add_class(m_const_holder_type, crep);
				}
			}

			// add methods
			for (std::map<const char*, detail::method_rep, detail::ltstr>::iterator i = m_methods.begin();
				i != m_methods.end(); 
				++i)
			{
				i->second.crep = crep;
			}
			std::swap(crep->m_methods, m_methods);

			// constructors
			m_constructor.swap(crep->m_constructor);

			#ifndef LUABIND_DONT_COPY_STRINGS
				assert(crep->m_strings.empty() && "Internal error");
				std::swap(crep->m_strings, m_strings);
			#endif

			std::swap(crep->m_getters, m_getters);
			std::swap(crep->m_setters, m_setters);

			for(int i = 0; i < detail::number_of_operators; ++i)
				std::swap(crep->m_operators[i], m_operators[i]);
	
			std::swap(crep->m_static_constants, m_static_constants);

			for (std::vector<base_desc>::iterator i = m_bases.begin();
							i != m_bases.end(); 
							++i)
			{
				detail::class_registry* r = detail::class_registry::get_registry(L);

				// the baseclass' class_rep structure
				detail::class_rep* bcrep = r->find_class(i->type);

				detail::class_rep::base_info base;
				base.pointer_offset = i->ptr_offset;
				base.base = bcrep;

				crep->add_base_class(base);
			}

			lua_settable(L, -3);
			lua_pop(L, 1);
		}


		// destructive copy
		virtual luabind::detail::scoped_object* clone()
		{
			assert(m_cloned == false);

#ifndef NDEBUG
			m_cloned = true;
#endif

			class_base* ret = new class_base(m_name);

			std::swap(ret->m_getters, m_getters);
			std::swap(ret->m_setters, m_setters);

			for(int i = 0; i < detail::number_of_operators; ++i)
				std::swap(ret->m_operators[i], m_operators[i]);
	
			std::swap(ret->m_static_constants, m_static_constants);
			ret->m_destructor = m_destructor;
			ret->m_extractor = m_extractor;
			ret->m_construct_held_type = m_construct_held_type;
			ret->m_held_type_size = m_held_type_size;
			ret->m_held_type_alignment = m_held_type_alignment;

			std::swap(ret->m_bases, m_bases);
			std::swap(ret->m_methods, m_methods);
			m_constructor.swap(ret->m_constructor);

			ret->m_name = m_name;

			ret->m_type = m_type;
			ret->m_held_type = m_held_type;
			ret->m_const_holder_type = m_const_holder_type;

#ifndef LUABIND_DONT_COPY_STRINGS
			std::swap(ret->m_strings, m_strings);
#endif

			return ret;
		}

	};











	// registers a class in the lua environment
	template<class T, class X1, class X2, class X3>
	struct class_: class_base 
	{
		typedef class_<T, X1, X2, X3> self_t;

		lua_State* m_L;

	private:

		template<class A, class B, class C, class D>
		class_(const class_<A,B,C,D>&);

	public:

		// WrappedType MUST inherit from T
		typedef typename detail::extract_parameter<
				boost::mpl::vector3<X1,X2,X3>
			,  boost::is_base_and_derived<T, boost::mpl::_>
	/*		,	boost::mpl::not_<
						boost::mpl::or_<
								detail::is_bases<boost::mpl::_>
							,	boost::is_base_and_derived<boost::mpl::_, T>
						>
				>*/
				,	detail::null_type
		>::type WrappedType;

		typedef typename detail::extract_parameter<
			  boost::mpl::vector3<X1,X2,X3>
			, boost::mpl::not_<
				boost::mpl::or_<
					boost::mpl::or_<
						  detail::is_bases<boost::mpl::_>
						, boost::is_base_and_derived<boost::mpl::_, T>
						>
					, boost::is_base_and_derived<T, boost::mpl::_>
				>
			>
		  , detail::null_type
		>::type HeldType;

		// this function generates conversion information
		// in the given class_rep structure. It will be able
		// to implicitly cast to the given template type
		template<class To>
		void gen_base_info(detail::type<To>)
		{
			// fist, make sure the given base class is registered.
			// if it's not registered we can't push it's lua table onto
			// the stack because it doesn't have a table

			// try to cast this type to the base type and remember
			// the pointer offset. For multiple inheritance the pointer
			// may change when casting. Since we need to be able to
			// cast we need this pointer offset.
			// store the information in this class' base class-vector
			base_desc base;
			base.type = LUABIND_TYPEID(To);
			base.ptr_offset = detail::ptr_offset(detail::type<T>(), detail::type<To>());
			add_base(base);
		}

		void gen_base_info(detail::type<detail::null_type>)
		{}

#define LUABIND_GEN_BASE_INFO(z, n, text) gen_base_info(detail::type<B##n>());

		template<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_BASES, class B)>
		void generate_baseclass_list(detail::type<bases<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_BASES, B)> >)
		{
			BOOST_PP_REPEAT(LUABIND_MAX_BASES, LUABIND_GEN_BASE_INFO, _)
		}

#undef LUABIND_GEN_BASE_INFO
	
		// this is the internal version of def() it is run from both overloads
		// of def. It has two versions, one where a contstructor is registered
		// and one where a function is registered
		template<class Policies>
		struct internal_def_s
		{
			template<class F>
			static void apply(const char* name, F f, class_base* c)
			{
//				std::cout << "HeldType2: " << typeid(HeldType).name() << "\n";

				detail::overload_rep o(f, static_cast<Policies*>(0));

				typedef LUABIND_MSVC_TYPENAME detail::function_callback_s<HeldType,T,F,Policies>::type call_t;

				o.set_match_fun(&detail::match_function_callback_s<T,F,Policies>::apply);
				o.call_fun = boost::bind<int>(call_t(f), _1, _2);

#ifndef LUABIND_NO_ERROR_CHECKING

				o.set_sig_fun(&detail::get_member_signature<F>::apply);

#endif

				c->add_method(name, o);
			}

			template<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, class A)>
			static void apply(constructor<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, A)>, class_base* c)
			{
//				std::cout << "HeldType2: " << typeid(HeldType).name() << "\n";

				detail::construct_rep::overload_t o;

				o.set_constructor(
					&detail::construct_class<
						 T
						,Policies
						,constructor<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, A)>
					>::apply
				);

				// if we have a WrappedType, we have to register it's constructor
				// but if it's null_type (no WrappedType) we should not register it
				detail::register_wrapped_type<WrappedType>::apply(o,
						static_cast<const constructor<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, A)>*>(0),
						static_cast<const Policies*>(0));


				o.set_match_fun(
					&detail::constructor_match<
						 constructor<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, A)>
						,2
						,Policies
					>::apply);

#ifndef LUABIND_NO_ERROR_CHECKING

				o.set_sig_fun(&detail::get_signature<constructor<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, A)> >::apply);

#endif

				typedef constructor<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, A)> con_t;

				o.set_arity(detail::calc_arity<con_t::arity>::apply(con_t(), static_cast<Policies*>(0)));

				c->add_constructor(o);
			}
		};

		class_(lua_State* L, const char* name): class_base(name), m_L(L) { init(); }
		class_(const char* name): class_base(name), m_L(0) { init(); }

		~class_()
		{
			if (m_L != 0)
			{
				scope::init(m_L);
				lua_pushvalue(m_L, LUA_GLOBALSINDEX);
				scope_stack::push(m_L);
				commit(m_L);
				scope_stack::pop(m_L);
			}
		}

		template<class F>
		class_& def(const char* name, F f)
		{
			internal_def_s<detail::null_type>::apply(name, f, this);
			return *this;
		}

		template<class F, class Policies>
		class_& def(const char* name, F f, const Policies&)
		{
			internal_def_s<Policies>::apply(name, f, this);
			return *this;
		}

		template<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, class A)>
		class_& def(constructor<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, A)> sig)
		{
			internal_def_s<detail::null_type>::apply(sig, this);
			return *this;
		}

		template<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, class A), class Policies>
		class_& def(constructor<BOOST_PP_ENUM_PARAMS(LUABIND_MAX_ARITY, A)> sig, const Policies& policies)
		{
			internal_def_s<Policies>::apply(sig, this);
			return *this;
		}

		template<class Getter>
		class_& property(const char* name, Getter g)
		{
			add_getter(name, boost::bind<int>(detail::get_caller<T, Getter, detail::null_type>(), _1, _2, g));
			return *this;
		}

		template<class Getter, class MaybeSetter>
		class_& property(const char* name, Getter g, MaybeSetter s)
		{
			return property_impl(name, g, s, boost::mpl::bool_<detail::is_policy_cons<MaybeSetter>::value>());
		}

		template<class Getter, class Setter, class GetPolicies>
		class_& property(const char* name, Getter g, Setter s, const GetPolicies& get_policies)
		{
			add_getter(name, boost::bind<int>(detail::get_caller<T, Getter, GetPolicies>(get_policies), _1, _2, g));
			add_setter(name, boost::bind<int>(detail::set_caller<T, Setter, detail::null_type>(), _1, _2, s));
			return *this;
		}

		template<class Getter, class Setter, class GetPolicies, class SetPolicies>
		class_& property(const char* name
									, Getter g, Setter s
									, const GetPolicies& get_policies
									, const SetPolicies& set_policies)
		{
			add_getter(name, boost::bind<int>(detail::get_caller<T, Getter, GetPolicies>(get_policies), _1, _2, g));
			add_setter(name, boost::bind<int>(detail::set_caller<T, Setter, GetPolicies>(set_policies), _1, _2, s));
			return *this;
		}

		template<class D>
		class_& def_readonly(const char* name, D T::*member_ptr)
		{
			add_getter(name, boost::bind<int>(detail::auto_get<T,D,detail::null_type>(), _1, _2, member_ptr));
			return *this;
		}

		template<class D, class Policies>
		class_& def_readonly(const char* name, D T::*member_ptr, const Policies& policies)
		{
			add_getter(name, boost::bind<int>(detail::auto_get<T,D,Policies>(policies), _1, _2, member_ptr));
			return *this;
		}

		template<class D>
		class_& def_readwrite(const char* name, D T::*member_ptr)
		{
			add_getter(name, boost::bind<int>(detail::auto_get<T,D,detail::null_type>(), _1, _2, member_ptr));
			add_setter(name, boost::bind<int>(detail::auto_set<T,D,detail::null_type>(), _1, _2, member_ptr));
			return *this;
		}

		template<class D, class GetPolicies>
		class_& def_readwrite(const char* name, D T::*member_ptr, const GetPolicies& get_policies)
		{
			add_getter(name, boost::bind<int>(detail::auto_get<T,D,GetPolicies>(get_policies), _1, _2, member_ptr));
			add_setter(name, boost::bind<int>(detail::auto_set<T,D,detail::null_type>(), _1, _2, member_ptr));
			return *this;
		}

		template<class D, class GetPolicies, class SetPolicies>
		class_& def_readwrite(const char* name, D T::*member_ptr, const GetPolicies& get_policies, const SetPolicies& set_policies)
		{
			add_getter(name, boost::bind<int>(detail::auto_get<T,D,GetPolicies>(get_policies), _1, _2, member_ptr));
			add_setter(name, boost::bind<int>(detail::auto_set<T,D,SetPolicies>(set_policies), _1, _2, member_ptr));
			return *this;
		}

		template<class op_id, class Left, class Right, class Policies>
		class_& def(detail::operator_<op_id, Left, Right>, const Policies& policies)
		{
			typedef typename detail::operator_unwrapper<Policies, op_id, T, Left, Right> op_type;
#ifndef LUABIND_NO_ERROR_CHECKING
			add_operator(op_type::get_id()
									, &op_type::execute
									, &op_type::match
									, &detail::get_signature<constructor<typename op_type::left_t, typename op_type::right_t> >::apply
									, detail::is_unary(op_type::get_id()) ? 1 : 2);
#else
			add_operator(op_type::get_id()
									, &op_type::execute
									, &op_type::match
									, detail::is_unary(op_type::get_id()) ? 1 : 2);
#endif
			return *this;
		}

		template<class op_id, class Left, class Right>
		class_& def(detail::operator_<op_id, Left, Right>)
		{
			typedef typename detail::operator_unwrapper<detail::null_type, op_id, T, Left, Right> op_type;

#ifndef LUABIND_NO_ERROR_CHECKING
			add_operator(op_type::get_id()
									, &op_type::execute
									, &op_type::match
									, &detail::get_signature<constructor<LUABIND_MSVC_TYPENAME op_type::left_t, LUABIND_MSVC_TYPENAME op_type::right_t> >::apply
									, detail::is_unary(op_type::get_id()) ? 1 : 2);
#else
			add_operator(op_type::get_id()
									, &op_type::execute
									, &op_type::match
									, detail::is_unary(op_type::get_id()) ? 1 : 2);
#endif
			return *this;
		}

		template<class Signature, bool Constant>
		class_& def(detail::application_operator<Signature, Constant>*)
		{
			typedef detail::application_operator<Signature, Constant, detail::null_type> op_t;

			int arity = detail::calc_arity<Signature::arity>::apply(Signature(), static_cast<detail::null_type*>(0));

#ifndef LUABIND_NO_ERROR_CHECKING
			add_operator(detail::op_call, &op_t::template apply<T>::execute, &op_t::match, &detail::get_signature<Signature>::apply, arity + 1);
#else
			add_operator(detail::op_call, &op_t::template apply<T>::execute, &op_t::match, arity + 1);
#endif

			return *this;
		}

		template<class Signature, bool Constant, class Policies>
		class_& def(detail::application_operator<Signature, Constant>*, const Policies& policies)
		{
			typedef detail::application_operator<Signature, Constant, Policies> op_t;

			int arity = detail::calc_arity<Signature::arity>::apply(Signature(), static_cast<Policies*>(0));

#ifndef LUABIND_NO_ERROR_CHECKING
			add_operator(detail::op_call, &op_t::template apply<T>::execute, &op_t::match, &detail::get_signature<Signature>::apply, arity + 1);
#else
			add_operator(detail::op_call, &op_t::template apply<T>::execute, &op_t::match, arity + 1);
#endif

			return *this;
		}

		detail::enum_maker<self_t> enum_(const char*)
		{
			return detail::enum_maker<self_t>(*this);
		}

	private:

		void init()
		{
			typedef typename detail::extract_parameter<
					boost::mpl::vector3<X1,X2,X3>
				,	boost::mpl::or_<
							detail::is_bases<boost::mpl::_>
						,	boost::is_base_and_derived<boost::mpl::_, T>
					>
				,	no_bases
			>::type bases_t;

			typedef typename 
				boost::mpl::if_<detail::is_bases<bases_t>
					,	bases_t
					,	bases<bases_t>
				>::type Base;
	
			set_type(LUABIND_TYPEID(T));
			set_held_type(detail::internal_held_type<HeldType>::apply());
			set_const_holder_type(get_const_holder(detail::type<HeldType>()));
			set_extractor(detail::internal_held_type_extractor<HeldType>::apply(detail::type<T>()));
			set_held_type_constructor(detail::internal_held_type_constructor<HeldType>::apply(detail::type<T>()));
			set_held_type_size(detail::internal_held_type_size<HeldType>::apply());
			set_destructor(detail::internal_held_type_destructor<HeldType>::apply(detail::type<T>()));

			set_held_type_alignment(detail::get_holder_alignment<HeldType>::apply());

			generate_baseclass_list(detail::type<Base>());
		}

		template<class Getter, class GetPolicies>
		class_& property_impl(const char* name,
									 Getter g,
									 GetPolicies policies,
									 boost::mpl::bool_<true>)
		{
			add_getter(name, boost::bind<int>(detail::get_caller<T,Getter,GetPolicies>(policies), _1, _2, g));
			return *this;
		}

		template<class Getter, class Setter>
		class_& property_impl(const char* name,
									 Getter g,
									 Setter s,
									 boost::mpl::bool_<false>)
		{
			add_getter(name, boost::bind<int>(detail::get_caller<T,Getter,detail::null_type>(), _1, _2, g));
			add_setter(name, boost::bind<int>(detail::set_caller<T,Setter,detail::null_type>(), _1, _2, s));
			return *this;
		}

	};
}

#endif // LUABIND_CLASS_HPP_INCLUDED

