#include "vulkax/compiler/expression.hpp"

#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace vulkax::compiler {
namespace {

using Node = std::shared_ptr<ExprNode>;

Node constant(double v) { auto n = std::make_shared<ExprNode>(); n->value = v; return n; }
Node unary(ExprKind k, Node a, std::string symbol = {}) { auto n = std::make_shared<ExprNode>(); n->kind=k; n->left=std::move(a); n->symbol=std::move(symbol); return n; }
Node binary(ExprKind k, Node a, Node b) { auto n = std::make_shared<ExprNode>(); n->kind=k; n->left=std::move(a); n->right=std::move(b); return n; }

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}
    ParseResult parse() {
        try {
            auto result = expression();
            skip();
            if (pos_ != text_.size()) throw std::runtime_error("unexpected token at offset " + std::to_string(pos_));
            return {std::move(result), {}};
        } catch (const std::exception& e) { return {nullptr, e.what()}; }
    }
private:
    std::string_view text_; std::size_t pos_{};
    void skip(){ while(pos_<text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_; }
    bool take(char c){ skip(); if(pos_<text_.size() && text_[pos_]==c){++pos_;return true;} return false; }
    Node expression(){ auto n=term(); for(;;){ if(take('+')) n=binary(ExprKind::Add,n,term()); else if(take('-')) n=binary(ExprKind::Subtract,n,term()); else return n; } }
    Node term(){ auto n=power(); for(;;){ if(take('*')) n=binary(ExprKind::Multiply,n,power()); else if(take('/')) n=binary(ExprKind::Divide,n,power()); else return n; } }
    Node power(){ auto n=unaryExpr(); if(take('^')) n=binary(ExprKind::Power,n,power()); return n; }
    Node unaryExpr(){ if(take('-')) return unary(ExprKind::Negate,unaryExpr()); if(take('+')) return unaryExpr(); return primary(); }
    Node primary(){
        skip();
        if(take('(')){ auto n=expression(); if(!take(')')) throw std::runtime_error("missing ')'"); return n; }
        if(pos_>=text_.size()) throw std::runtime_error("unexpected end of expression");
        if(std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_]=='.'){
            const auto begin=pos_; bool exp=false;
            while(pos_<text_.size()){
                char c=text_[pos_];
                if(std::isdigit(static_cast<unsigned char>(c)) || c=='.') ++pos_;
                else if((c=='e'||c=='E')&&!exp){exp=true;++pos_; if(pos_<text_.size()&&(text_[pos_]=='+'||text_[pos_]=='-'))++pos_;}
                else break;
            }
            return constant(std::stod(std::string(text_.substr(begin,pos_-begin))));
        }
        if(std::isalpha(static_cast<unsigned char>(text_[pos_])) || text_[pos_]=='_'){
            const auto begin=pos_++;
            while(pos_<text_.size() && (std::isalnum(static_cast<unsigned char>(text_[pos_])) || text_[pos_]=='_')) ++pos_;
            std::string name(text_.substr(begin,pos_-begin));
            if(take('(')){ auto arg=expression(); if(!take(')')) throw std::runtime_error("missing ')' after function"); return unary(ExprKind::Function,arg,std::move(name)); }
            auto n=std::make_shared<ExprNode>(); n->kind=ExprKind::Variable; n->symbol=std::move(name); return n;
        }
        throw std::runtime_error("unexpected character at offset " + std::to_string(pos_));
    }
};

bool equalDimension(const units::Dimension& a, const units::Dimension& b){ return a==b; }
units::Dimension mulDim(units::Dimension a,const units::Dimension& b){ for(std::size_t i=0;i<a.exponents.size();++i)a.exponents[i]+=b.exponents[i]; return a; }
units::Dimension divDim(units::Dimension a,const units::Dimension& b){ for(std::size_t i=0;i<a.exponents.size();++i)a.exponents[i]-=b.exponents[i]; return a; }
units::Dimension powDim(units::Dimension a,int p){ for(auto& e:a.exponents)e*=p; return a; }

} // namespace

ParseResult parseExpression(std::string_view source){ return Parser(source).parse(); }

double evaluateExpression(const ExprNode& n,const std::unordered_map<std::string,double>& v){
    const auto e=[&](const std::shared_ptr<ExprNode>& p){ if(!p) throw std::invalid_argument("malformed expression"); return evaluateExpression(*p,v); };
    switch(n.kind){
    case ExprKind::Constant:return n.value;
    case ExprKind::Variable:{auto it=v.find(n.symbol);if(it==v.end())throw std::invalid_argument("missing variable: "+n.symbol);return it->second;}
    case ExprKind::Add:return e(n.left)+e(n.right); case ExprKind::Subtract:return e(n.left)-e(n.right);
    case ExprKind::Multiply:return e(n.left)*e(n.right); case ExprKind::Divide:return e(n.left)/e(n.right);
    case ExprKind::Negate:return -e(n.left); case ExprKind::Power:return std::pow(e(n.left),e(n.right));
    case ExprKind::Function:{double x=e(n.left); if(n.symbol=="sin")return std::sin(x); if(n.symbol=="cos")return std::cos(x); if(n.symbol=="exp")return std::exp(x); if(n.symbol=="log")return std::log(x); if(n.symbol=="sqrt")return std::sqrt(x); if(n.symbol=="abs")return std::abs(x); throw std::invalid_argument("unknown function: "+n.symbol);}
    }
    return 0.0;
}

std::shared_ptr<ExprNode> differentiate(const ExprNode& n,std::string_view x){
    const auto d=[&](const std::shared_ptr<ExprNode>& p){return differentiate(*p,x);};
    const auto clone=[&](const std::shared_ptr<ExprNode>& p){return p;};
    switch(n.kind){
    case ExprKind::Constant:return constant(0.0);
    case ExprKind::Variable:return constant(n.symbol==x?1.0:0.0);
    case ExprKind::Add:return binary(ExprKind::Add,d(n.left),d(n.right));
    case ExprKind::Subtract:return binary(ExprKind::Subtract,d(n.left),d(n.right));
    case ExprKind::Multiply:return binary(ExprKind::Add,binary(ExprKind::Multiply,d(n.left),clone(n.right)),binary(ExprKind::Multiply,clone(n.left),d(n.right)));
    case ExprKind::Divide:return binary(ExprKind::Divide,binary(ExprKind::Subtract,binary(ExprKind::Multiply,d(n.left),clone(n.right)),binary(ExprKind::Multiply,clone(n.left),d(n.right))),binary(ExprKind::Power,clone(n.right),constant(2.0)));
    case ExprKind::Negate:return unary(ExprKind::Negate,d(n.left));
    case ExprKind::Power:
        if(n.right && n.right->kind==ExprKind::Constant) return binary(ExprKind::Multiply,binary(ExprKind::Multiply,constant(n.right->value),binary(ExprKind::Power,clone(n.left),constant(n.right->value-1.0))),d(n.left));
        throw std::invalid_argument("symbolic derivative supports only constant powers");
    case ExprKind::Function:{
        Node outer;
        if(n.symbol=="sin") outer=unary(ExprKind::Function,clone(n.left),"cos");
        else if(n.symbol=="cos") outer=unary(ExprKind::Negate,unary(ExprKind::Function,clone(n.left),"sin"));
        else if(n.symbol=="exp") outer=unary(ExprKind::Function,clone(n.left),"exp");
        else if(n.symbol=="log") outer=binary(ExprKind::Divide,constant(1.0),clone(n.left));
        else if(n.symbol=="sqrt") outer=binary(ExprKind::Divide,constant(0.5),unary(ExprKind::Function,clone(n.left),"sqrt"));
        else throw std::invalid_argument("derivative unsupported for function: "+n.symbol);
        return binary(ExprKind::Multiply,outer,d(n.left));
    }} return constant(0.0);
}

std::string canonicalExpression(const ExprNode& n){
    std::ostringstream out; out<<std::setprecision(17);
    const auto c=[&](const std::shared_ptr<ExprNode>& p){return canonicalExpression(*p);};
    switch(n.kind){case ExprKind::Constant:out<<n.value;break;case ExprKind::Variable:out<<n.symbol;break;case ExprKind::Add:out<<"("<<c(n.left)<<"+"<<c(n.right)<<")";break;case ExprKind::Subtract:out<<"("<<c(n.left)<<"-"<<c(n.right)<<")";break;case ExprKind::Multiply:out<<"("<<c(n.left)<<"*"<<c(n.right)<<")";break;case ExprKind::Divide:out<<"("<<c(n.left)<<"/"<<c(n.right)<<")";break;case ExprKind::Negate:out<<"(-"<<c(n.left)<<")";break;case ExprKind::Power:out<<"("<<c(n.left)<<"^"<<c(n.right)<<")";break;case ExprKind::Function:out<<n.symbol<<"("<<c(n.left)<<")";break;} return out.str();
}

std::optional<units::Dimension> inferDimension(const ExprNode& n,const std::unordered_map<std::string,units::Dimension>& symbols,std::string* diagnostic){
    const auto fail=[&](std::string m)->std::optional<units::Dimension>{if(diagnostic)*diagnostic=std::move(m);return std::nullopt;};
    if(n.kind==ExprKind::Constant)return units::dimensionless;
    if(n.kind==ExprKind::Variable){auto it=symbols.find(n.symbol);if(it==symbols.end())return fail("unknown dimension for "+n.symbol);return it->second;}
    if(n.kind==ExprKind::Negate)return inferDimension(*n.left,symbols,diagnostic);
    if(n.kind==ExprKind::Function){auto d=inferDimension(*n.left,symbols,diagnostic);if(!d)return std::nullopt;if(!equalDimension(*d,units::dimensionless))return fail("function "+n.symbol+" requires dimensionless argument");return units::dimensionless;}
    auto a=inferDimension(*n.left,symbols,diagnostic);auto b=inferDimension(*n.right,symbols,diagnostic);if(!a||!b)return std::nullopt;
    if(n.kind==ExprKind::Add||n.kind==ExprKind::Subtract){if(!equalDimension(*a,*b))return fail("addition/subtraction dimension mismatch");return a;}
    if(n.kind==ExprKind::Multiply)return mulDim(*a,*b); if(n.kind==ExprKind::Divide)return divDim(*a,*b);
    if(n.kind==ExprKind::Power){if(n.right->kind!=ExprKind::Constant)return fail("dimensional power exponent must be constant");double rounded=std::round(n.right->value);if(std::abs(rounded-n.right->value)>1e-12)return fail("dimensional power exponent must be integer");return powDim(*a,static_cast<int>(rounded));}
    return fail("unsupported dimension inference node");
}

} // namespace vulkax::compiler
