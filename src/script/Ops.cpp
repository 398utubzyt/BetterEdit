#include "Parser.hpp"

using namespace script;

Result<Rc<UnOpExpr>> UnOpExpr::pull(InputStream& stream, Attrs& attrs) {
    Rollback rb(stream);
    GEODE_UNWRAP_INTO(auto op, Token::pull<Op>(stream));
    if (!isUnOp(op)) {
        return Err(fmt::format("Invalid unary operator '{}'", tokenToString(op)));
    }
    GEODE_UNWRAP_INTO(auto expr, Expr::pull(stream, attrs));
    return make<UnOpExpr>({
        .expr = expr,
        .op = op,
        .src = rb.commit(),
    });
}

Result<Rc<Value>> UnOpExpr::eval(State& state) {
    GEODE_UNWRAP_INTO(auto value, expr->eval(state));
    switch (op) {
        case Op::Not: {
            return Ok(Value::rc(!value->truthy()));
        } break;

        case Op::Add: {
            if (auto num = value->has<NumLit>()) {
                return Ok(Value::rc(+*num));
            }
            return Err(fmt::format(
                "Attempted to use unary plus operator on value of type {}", 
                value->typeName()
            ));
        } break;

        case Op::Sub: {
            if (auto num = value->has<NumLit>()) {
                return Ok(Value::rc(-*num));
            }
            return Err(fmt::format(
                "Attempted to use unary minus operator on value of type {}", 
                value->typeName()
            ));
        } break;

        default: {
            throw std::runtime_error(fmt::format(
                "Internal error: Unimplemented unary operator {}",
                tokenToString(op, true)
            ));
        } break;
    }
}

std::string UnOpExpr::debug() const {
    return fmt::format(
        "UnOpExpr({}, {})",
        tokenToString(op, true), expr->debug()
    );
}

Result<Rc<CallExpr>> CallExpr::pull(Rc<Expr> before, InputStream& stream, Attrs& attrs) {
    Rollback rb(stream);
    GEODE_UNWRAP(Token::pull('(', stream));
    // handle ()
    if (Token::pull(')', stream)) {
        return make<CallExpr>({
            .expr = before,
            .args = {},
            .named = {},
            .src = before->src(true) + rb.commit(),
        });
    }
    std::vector<Rc<Expr>> args;
    std::unordered_map<std::string, Rc<Expr>> named;
    while (true) {
        tickExecutionCounter();
        bool isNamed = false;
        Rollback namedrb(stream);
        // named args are in the form `<ident> = <expr>`
        if (auto ident = Token::pull<Ident>(stream)) {
            if (Token::pull(Op::Seq, stream)) {
                GEODE_UNWRAP_INTO(auto value, Expr::pull(stream, attrs));
                if (named.count(ident.unwrap())) {
                    return Err(fmt::format(
                        "Named argument '{}' has already been passed",
                        ident.unwrap()
                    ));
                }
                named.insert({ ident.unwrap(), value });
                isNamed = true;
            }
            // if no eq operator, then this is a normal binop arg. whoops!
        }
        if (!isNamed) {
            namedrb.ret();
            GEODE_UNWRAP_INTO(auto expr, Expr::pull(stream, attrs));
            args.push_back(expr);
        }
        namedrb.commit();
        // allow trailing comma
        if (!Token::pull(',', stream) || Token::peek(')', stream)) {
            break;
        }
    }
    GEODE_UNWRAP(Token::pull(')', stream));
    return make<CallExpr>({
        .expr = before,
        .args = args,
        .named = named,
        .src = before->src(true) + rb.commit(),
    });
}

Result<Rc<Value>> CallExpr::eval(State& state) {
    GEODE_UNWRAP_INTO(auto val, expr->eval(state));
    auto funp = std::get_if<Rc<const FunExpr>>(&val->value);
    if (!funp) {
        return Err(
            "Attempted to call {} as a function\n * {} evaluated to null",
            val->typeName(), expr->src()
        );
    }
    auto fun = *funp;

    std::unordered_map<std::string, Rc<Value>> res;
    Dict namedArgs;
    for (auto& [name, nexpr] : named) {
        GEODE_UNWRAP_INTO(auto val, nexpr->eval(state));
        res.insert({ name, val });
        if (fun->variadic) {
            namedArgs.insert({ name, *val });
        }
    }
    if (fun->variadic) {
        res.insert({ "namedArguments", Value::rc(namedArgs) });
    }

    if (fun->variadic) {
        auto i = 0u;
        Array positional;
        for (auto& arg : args) {
            if (i < fun->params.size()) {
                auto name = fun->params.at(i).first;
                auto arg = args.at(i);
                if (res.count(name)) {
                    return Err(fmt::format(
                        "Argument '{}' passed multiple times",
                        name
                    ));
                }
                GEODE_UNWRAP_INTO(auto val, arg->eval(state));
                res.insert({ name, val });
            }
            else {
                GEODE_UNWRAP_INTO(auto val, arg->eval(state));
                res.insert({ fmt::format("argument{}", i - fun->params.size()), val });
                positional.push_back(*val);
            }
            i += 1;
        }
        res.insert({ fun->variadic.value(), Value::rc(positional) });
    }
    else {
        auto i = 0u;
        for (auto& [name, _] : fun->params) {
            if (args.size() > i) {
                auto arg = args.at(i);
                if (res.count(name)) {
                    return Err(fmt::format(
                        "Argument '{}' passed multiple times",
                        name
                    ));
                }
                GEODE_UNWRAP_INTO(auto val, arg->eval(state));
                res.insert({ name, val });
                i++;
            }
            else {
                break;
            }
        }
        if (args.size() != i) {
            return Err("Function called with too many arguments");
        }
    }

    // insert default values
    for (auto& [name, def] : fun->params) {
        if (!res.count(name)) {
            if (!def) {
                return Err(fmt::format("Missing required parameter '{}'", name));
            }
            else {
                GEODE_UNWRAP_INTO(auto val, def.value()->eval(state));
                res.insert({ name, val });
            }
        }
    }

    auto scope = state.scope();
    for (auto& [p, value] : res) {
        state.add(p, value);
    }
    try {
        GEODE_UNWRAP_INTO(auto ret, fun->body->eval(state));
        return Ok(ret);
    }
    catch(ReturnSignal& sig) {
        return Ok(sig.value);
    }
}

std::string CallExpr::debug() const {
    std::string a;
    bool first = true;
    for (auto& arg : args) {
        if (!first) {
            a += ", ";
        }
        first = false;
        a += arg->debug();
    }
    std::string n;
    first = true;
    for (auto& [na, va] : named) {
        if (!first) {
            n += ", ";
        }
        first = false;
        n += fmt::format("Named({}, {})", na, va->debug());
    }
    return fmt::format("CallExpr({}, args({}), named({}))", expr->debug(), a, n);
}

Result<Rc<IndexExpr>> IndexExpr::pull(Rc<Expr> before, InputStream& stream, Attrs& attrs) {
    Rollback rb(stream);
    GEODE_UNWRAP(Token::pull('[', stream));
    if (Token::peek(']', stream)) {
        return Err("Expected value for array index");
    }
    GEODE_UNWRAP_INTO(auto index, Expr::pull(stream, attrs));
    GEODE_UNWRAP(Token::pull(']', stream));
    return make<IndexExpr>({
        .expr = before,
        .index = index,
        .src = before->src(true) + rb.commit(),
    });
}

Result<Rc<Value>> IndexExpr::eval(State& state) {
    GEODE_UNWRAP_INTO(auto value, expr->eval(state));
    GEODE_UNWRAP_INTO(auto ixval, index->eval(state));
    if (auto rarr = value->has<Array>()) {
        auto arr = *rarr;
        auto rix = ixval->has<NumLit>();
        if (!rix) {
            return Err(
                "Attempted to index array with a non-number\n * {} evaluated into {}",
                index->src(), ixval->typeName()
            );
        }
        if (*rix < 0.0) {
            return Err("Index is negative ({})", *rix);
        }
        auto ix = static_cast<size_t>(round(*rix));
        if (ix >= arr.size()) {
            return Err("Index is past array bounds ({} >= {})", ix, arr.size());
        }
        return Ok(Rc<Value>(new Value(arr.at(ix))));
    }
    else if (value->has<Dict>() || value->has<Ref<GameObject>>()) {
        auto rix = ixval->has<StrLit>();
        if (!rix) {
            return Err(
                "Attempted to index dictionary or object with a non-string\n * {} evaluated into {}",
                index->src(), ixval->typeName()
            );
        }
        auto mem = value->member(*rix);
        if (!mem) {
            return Err("Dictionary or object has no member '{}'", *rix);
        }
        return Ok(*mem);
    }
    else {
        return Err(
            "Attempted to index into a non array, dictionary or object\n * {} evaluated into {}", 
            expr->src(), value->typeName()
        );
    }
}

std::string IndexExpr::debug() const {
    return fmt::format("IndexExpr({}, {})", expr->debug(), index->debug());
}

Result<Rc<MemberExpr>> MemberExpr::pull(Rc<Expr> before, InputStream& stream, Attrs& attrs) {
    Rollback rb(stream);
    GEODE_UNWRAP(Token::pull('.', stream));
    GEODE_UNWRAP_INTO(auto member, Token::pull<Ident>(stream));
    return make<MemberExpr>({
        .expr = before,
        .member = member,
        .src = before->src(true) + rb.commit(),
    });
}

Result<Rc<Value>> MemberExpr::eval(State& state) {
    GEODE_UNWRAP_INTO(auto value, expr->eval(state));
    auto mem = value->member(member);
    if (!mem) {
        return Err("{} has no member {}", value->typeName(), member);
    }
    return Ok(mem.value());
}

std::string MemberExpr::debug() const {
    return fmt::format("MemberExpr({}, {})", expr->debug(), member);
}

Result<Rc<ForEachExpr>> ForEachExpr::pull(Rc<Expr> before, InputStream& stream, Attrs& attrs) {
    Rollback rb(stream);
    GEODE_UNWRAP(Token::pull("...", stream));

    // pull an identifier but don't consume it
    Rollback irb(stream);
    GEODE_UNWRAP_INTO(auto member, Token::pull<Ident>(stream));
    irb.ret();

    GEODE_UNWRAP_INTO(auto expr, Expr::pull(stream, attrs));

    return make<ForEachExpr>({
        .target = before,
        .member = member,
        .expr = expr,
        .src = before->src(true) + rb.commit(),
    });
}

Result<Rc<Value>> ForEachExpr::eval(State& state) {
    GEODE_UNWRAP_INTO(auto value, target->eval(state));
    auto arr = value->has<Array>();
    if (!arr) {
        return Err(
            "Cannot use foreach operator on {}, expected array",
            value->typeName()
        );
    }
    auto res = Value::rc(NullLit());
    for (auto& val : *arr) {
        auto mem = val.member(member);
        if (!mem) {
            return Err("{} has no member {}", val.typeName(), member);
        }
        auto _ = state.scope();
        state.add(member, mem.value());
        GEODE_UNWRAP_INTO(res, expr->eval(state));
    }
    return Ok(res);
}

std::string ForEachExpr::debug() const {
    return fmt::format("ForEachExpr({}, {}, {})", target->debug(), member, expr->debug());
}
