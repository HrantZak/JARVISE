#include "SceneController.h"
#include "Expression.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QColor>
#include <QPointF>
#include <QRegularExpression>
#include <cmath>

namespace jarvis::scene3d {
QString SceneController::validateDrawing(const QString& json) {
    if(json.size()>100000) return QStringLiteral("Слишком большой рисунок.");
    const auto doc=QJsonDocument::fromJson(json.toUtf8());
    if(!doc.isObject()) return QStringLiteral("Нужен JSON рисунка.");
    const auto root=doc.object();
    if(!root["title"].isString() || root["title"].toString().size()>160) return QStringLiteral("Нужно короткое название.");
    const auto paths=root["paths"].toArray();
    const auto labels=root["labels"].toArray();
    if((paths.isEmpty() && labels.isEmpty()) || paths.size()>200 || labels.size()>40) return QStringLiteral("Нужно до 200 линий и до 40 подписей.");
    for(const auto& entry:labels) {
        const auto label=entry.toObject();
        if(!label["text"].isString() || label["text"].toString().isEmpty() || label["text"].toString().size()>160) return QStringLiteral("Подпись должна содержать до 160 символов.");
        for(const auto* key:{"x","y"}) if(!label[key].isDouble() || !std::isfinite(label[key].toDouble()) || std::abs(label[key].toDouble())>1000) return QStringLiteral("Неверная позиция текста.");
        if(label.contains("at") && (!label["at"].isDouble() || label["at"].toDouble()<0 || label["at"].toDouble()>1)) return QStringLiteral("Неверный момент появления подписи.");
    }
    int count=0;
    for(const auto& entry:paths) {
        const auto line=entry.toObject();
        if(!QColor(line["color"].toString("#65cfff")).isValid()) return QStringLiteral("Неверный цвет.");
        const auto points=line["points"].toArray();
        if(points.size()<2) return QStringLiteral("В линии нужны хотя бы две точки.");
        count+=static_cast<int>(points.size());
        if(count>5000) return QStringLiteral("Лимит — 5000 точек.");
        for(const auto& point:points) {
            const auto p=point.toArray();
            if(p.size()!=2 && p.size()!=3) return QStringLiteral("Точка должна иметь 2 или 3 координаты.");
            for(const auto& v:p) if(!v.isDouble() || !std::isfinite(v.toDouble()) || std::abs(v.toDouble())>1000) return QStringLiteral("Неверная координата.");
        }
    }
    return {};
}
bool SceneController::applyDrawing(const QString& json) {
    const auto error=validateDrawing(json);
    if(!error.isEmpty()) {m_status=error;emit changed();return false;}
    const auto root=QJsonDocument::fromJson(json.toUtf8()).object();
    m_strokes=root["paths"].toArray().toVariantList();m_drawingTitle=root["title"].toString();m_drawingMode=true;
    m_labels=root["labels"].toArray().toVariantList();
    m_status=QStringLiteral("Рисую: ")+m_drawingTitle;
    emit drawingChanged();emit changed();emit showRequested();return true;
}
std::optional<QString> SceneController::drawRequest(const QString& text) {
    if(QRegularExpression(QStringLiteral("^(объясни|обьясни|обясни) (?:мне )?как работает видеокарта[?.!]*$")).match(text).hasMatch()) {
        QJsonArray paths,labels;
        const QStringList captions{QStringLiteral("1. Команды от процессора\nЧто и как нужно нарисовать"),QStringLiteral("2. Видеопамять\nХранит текстуры и данные"),QStringLiteral("3. Графический процессор\nВычисляет геометрию и цвет"),QStringLiteral("4. Готовый кадр\nПередаётся на монитор")};
        const QPointF positions[]={{-4,2.5},{4,2.5},{4,-2.5},{-4,-2.5}};
        for(int i=0;i<4;++i){const double x=positions[i].x(),y=positions[i].y();
            QJsonArray outline;
            if(i==1) {
                for(int n=0;n<=80;++n){const double a=n*6.28318530718/80;outline.append(QJsonArray{x+3.4*std::cos(a),y+1.3*std::sin(a)});}
            } else {
                const double radius=i==2?.18:.55;
                for(int corner=0;corner<4;++corner){const double cx=x+(corner==0||corner==3?1:-1)*(3.4-radius),cy=y+(corner<2?1:-1)*(1.3-radius);for(int n=0;n<=10;++n){const double a=(corner*90+n*9)*3.14159265359/180;outline.append(QJsonArray{cx+radius*std::cos(a),cy+radius*std::sin(a)});}}
                outline.append(outline.first());
            }
            paths.append(QJsonObject{{"points",outline},{"color",i==1?"#9d9aff":i==2?"#61e5c1":"#65cfff"}});
            labels.append(QJsonObject{{"text",captions[i]},{"x",x},{"y",y},{"at",i*.25+.12}});
            if(i<3){const double ax=i==0?-.6:i==1?4:.6,ay=i==1?1.2:i==0?2.5:-2.5;const double bx=i==0?.6:i==1?4:-.6,by=i==1?-1.2:ay;
                paths.append(QJsonObject{{"points",QJsonArray{QJsonArray{ax,ay},QJsonArray{bx,by}}}});
                const double dx=bx-ax,dy=by-ay,len=std::hypot(dx,dy),ux=dx/len,uy=dy/len;
                paths.append(QJsonObject{{"points",QJsonArray{QJsonArray{bx-.35*ux+.22*uy,by-.35*uy-.22*ux},QJsonArray{bx,by},QJsonArray{bx-.35*ux-.22*uy,by-.35*uy+.22*ux}}}});
            }
        }
        applyDrawing(QString::fromUtf8(QJsonDocument(QJsonObject{{"title",QStringLiteral("Как работает видеокарта · упрощённая схема")},{"paths",paths},{"labels",labels}}).toJson()));
        return QStringLiteral("Показываю по шагам.");
    }
    const QRegularExpression intent(QStringLiteral("^(нарисуй|рисуй|начерти|изобрази|дорисуй)\\b"),QRegularExpression::UseUnicodePropertiesOption);
    if(!intent.match(text).hasMatch()) return std::nullopt;
    QJsonArray paths;
    auto line=[&](QJsonArray points,QString color="#65cfff") { paths.append(QJsonObject{{"points",points},{"color",color}}); };
    auto rect=[&](double x,double y,double w,double h){line({QJsonArray{x,y},QJsonArray{x+w,y},QJsonArray{x+w,y+h},QJsonArray{x,y+h},QJsonArray{x,y}});};
    auto circle=[&](double x,double y,double r){QJsonArray p;for(int i=0;i<=64;++i){const double a=i*6.28318530718/64;p.append(QJsonArray{x+r*std::cos(a),y+r*std::sin(a)});}line(p);};
    QString title;
    // Only exact, simple requests use templates. Rich descriptions go to the model.
    const auto noun=text.section(' ',1).trimmed();
    if(QRegularExpression(QStringLiteral("^(?:линии? )?(?:синуса|синус|синусоиду|синусоида)$")).match(noun).hasMatch() || noun.startsWith("y =") || noun.startsWith("y=")) {
        QString formula=noun.startsWith('y')?noun.section('=',1).trimmed():"sin(x)";
        try {
            Expression expression(formula);
            line({QJsonArray{-6.5,0},QJsonArray{6.5,0}},"#38536a");line({QJsonArray{0,-1.7},QJsonArray{0,1.7}},"#38536a");
            QJsonArray points;
            for(int i=0;i<=400;++i){const double x=-6.28+i*12.56/400,y=expression.value(x,0);if(std::isfinite(y)&&std::abs(y)<100) points.append(QJsonArray{x,y});else {if(points.size()>1)line(points);points={};}}
            if(points.size()>1)line(points);title="y = "+formula;
        } catch(const std::exception&) {return QStringLiteral("Не удалось разобрать формулу.");}
    } else if(noun==QStringLiteral("дом") || noun==QStringLiteral("домик")) {
        rect(-2,-1.5,4,2.8);line({QJsonArray{-2.4,1.3},QJsonArray{0,3.1},QJsonArray{2.4,1.3},QJsonArray{-2.4,1.3}});
        rect(-.4,-1.5,.8,1.6);rect(-1.6,0,.8,.8);rect(.8,0,.8,.8);
        line({QJsonArray{-1.2,0},QJsonArray{-1.2,.8}});line({QJsonArray{1.2,0},QJsonArray{1.2,.8}});
        line({QJsonArray{-2,1.3},QJsonArray{-3,2.1},QJsonArray{-.9,3.9},QJsonArray{0,3.1}});
        line({QJsonArray{-3,2.1},QJsonArray{-3,-.7},QJsonArray{-2,-1.5}});title=QStringLiteral("Дом");
    } else if(QRegularExpression(QStringLiteral("^(джойстик|жостик|геймпад|контроллер)$")).match(noun).hasMatch()) {
        line({QJsonArray{-2.3,.9},QJsonArray{-1.4,1.2},QJsonArray{1.4,1.2},QJsonArray{2.3,.9},QJsonArray{2.9,-1.1},QJsonArray{2.6,-1.5},QJsonArray{2.1,-1.4},QJsonArray{1.3,-.5},QJsonArray{-1.3,-.5},QJsonArray{-2.1,-1.4},QJsonArray{-2.6,-1.5},QJsonArray{-2.9,-1.1},QJsonArray{-2.3,.9}});
        line({QJsonArray{-1.8,.25},QJsonArray{-1.4,.25},QJsonArray{-1.4,-.15},QJsonArray{-1.1,-.15},QJsonArray{-1.1,.25},QJsonArray{-.7,.25},QJsonArray{-.7,.55},QJsonArray{-1.1,.55},QJsonArray{-1.1,.95},QJsonArray{-1.4,.95},QJsonArray{-1.4,.55},QJsonArray{-1.8,.55},QJsonArray{-1.8,.25}});
        circle(-.65,-.3,.28);circle(.65,-.3,.28);
        for(const auto p:{QPointF{1.5,.9},QPointF{1.9,.5},QPointF{1.5,.1},QPointF{1.1,.5}})circle(p.x(),p.y(),.14);
        rect(-.35,.3,.7,.4);title=QStringLiteral("Геймпад");
    } else return std::nullopt;
    applyDrawing(QString::fromUtf8(QJsonDocument(QJsonObject{{"title",title},{"paths",paths}}).toJson(QJsonDocument::Compact)));
    return QStringLiteral("Рисую.");
}
}
