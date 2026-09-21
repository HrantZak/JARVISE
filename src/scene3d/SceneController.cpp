#include "SceneController.h"
#include "Expression.h"
#include "Mesh.h"
#include <QColor>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QTextStream>
#include <algorithm>
#include <QFutureWatcher>
#include <QtConcurrent>

namespace jarvis::scene3d {
namespace {
bool contains(const QString& text,const QString& pattern) {
    return QRegularExpression(pattern,QRegularExpression::CaseInsensitiveOption|QRegularExpression::UseUnicodePropertiesOption).match(text).hasMatch();
}
QString colorIn(const QString& text) {
    const auto hex=QRegularExpression("#[0-9a-fA-F]{6}\\b").match(text);
    if(hex.hasMatch()) return hex.captured();
    const std::pair<const char*,const char*> colors[]={{"красн|red","#e25959"},{"син(ий|юю|его)|blue","#408cff"},{"зел[её]н|green","#58c58a"},
        {"бел|white","#e8edf2"},{"ч[её]рн|black","#30343c"},{"золот|gold","#d3a34f"},{"фиолет|purple","#a87bee"},{"оранж|orange","#f09a48"}};
    for(const auto& [pattern,color]:colors) if(contains(text,QString::fromUtf8(pattern))) return color;
    return {};
}
QString materialIn(const QString& text) {
    if(contains(text,QStringLiteral("стекл|glass"))) return "glass";
    if(contains(text,QStringLiteral("металл|metal"))) return "metal";
    if(contains(text,QStringLiteral("пластик|plastic"))) return "plastic";
    return {};
}
}
void SceneController::setSelected(int value) { if(value>=0 && value<m_items.size()) {m_selected=value; emit changed();} }
void SceneController::setLighting(double value) { m_lighting=std::clamp(value,0.2,3.0); emit changed(); }
void SceneController::clearScene() { m_items.clear();m_selected=-1;m_status=QStringLiteral("Сцена очищена.");emit changed(); }
void SceneController::removeSelected() {
    if(m_selected<0 || m_selected>=m_items.size()) return;
    m_items.removeAt(m_selected);m_selected=static_cast<int>(m_items.size())-1;emit changed();
}
bool SceneController::budget(const QVariantList& items) const {
    qint64 triangles=0;
    for(const auto& value:items) {
        const auto spec=value.toMap(); const qint64 n=spec.value("resolution",48).toInt(); const auto kind=spec.value("kind").toString();
        triangles+=kind=="cube"?12:kind=="house"?100:kind=="mug"?n*n+12*n:kind=="cylinder"?6*n:kind=="torus"?n*n:2*n*n;
    }
    return items.size()<=8 && triangles<=1000000;
}
bool SceneController::setParameter(const QString& key,const QVariant& value) {
    if(m_selected<0 || m_selected>=m_items.size()) return false;
    auto spec=m_items[m_selected].toMap();
    if(key=="color") {const QColor color(value.toString());if(!color.isValid()) return false;spec[key]=color.name();}
    else if(key=="material") {if(!QStringList{"plastic","metal","glass"}.contains(value.toString())) return false;spec[key]=value;}
    else if(key=="texture") {if(!QStringList{"none","checker"}.contains(value.toString())) return false;spec[key]=value;}
    else if(key=="resolution") spec[key]=std::clamp(value.toInt(),8,700);
    else if(key=="width" || key=="height" || key=="depth") {const double number=value.toDouble();if(!std::isfinite(number)) return false;spec[key]=std::clamp(number,0.1,5.0);}
    else return false;
    auto candidate=m_items;candidate[m_selected]=spec;
    if(!budget(candidate)) {m_status=QStringLiteral("Лимит сцены — 1 миллион треугольников. Уменьшите детализацию или удалите модель.");emit changed();return false;}
    m_items=candidate;m_status=QStringLiteral("Параметры обновлены.");emit changed();return true;
}
std::optional<QString> SceneController::handle(const QString& request) {
    QString text=request.trimmed().toLower();
    text.remove(QRegularExpression(QStringLiteral("^(?:джарвис|жарвис|jarvis)[, :]*")));
    if(auto drawing=drawRequest(text)) return drawing;
    if(contains(text,QStringLiteral("^(нарисуй|рисуй|начерти|изобрази|дорисуй)\\b"))) return std::nullopt;
    if(text.startsWith(QStringLiteral("экспортируй "))) text.replace(0,11,QStringLiteral("экспорт "));
    const bool sceneMention=contains(text,QStringLiteral("\\b(3d|3д|модел\\w*|сцен\\w*|куб\\w*|сфер\\w*|тор|торус|цилиндр\\w*|кружк\\w*|дом|дома|домик|график\\w*|cube|sphere|house|mug|torus|cylinder|surface)\\b"));
    if(!sceneMention) return std::nullopt;
    const bool verb=contains(text,QStringLiteral("^(?:покажи|показать|создай|сделай|построй|отобрази|добавь|открой|вращай|останови|сбрось|очисти|удали|измени|покрась|увеличь|уменьши|включи|выключи|экспорт|show|create|add|rotate|stop|reset|clear)\\b"));
    if(!verb) return std::nullopt;
    if(contains(text,QStringLiteral("(экспорт)"))) { requestExport(text.contains("obj")?"obj":"gltf");return m_status; }
    if(contains(text,QStringLiteral("^(очисти|clear)"))) {clearScene();emit showRequested();return m_status;}
    if(contains(text,QStringLiteral("^(удали)"))) {removeSelected();emit showRequested();return QStringLiteral("Модель удалена.");}
    if(contains(text,QStringLiteral("^(сбрось|reset)"))) {emit cameraReset();emit showRequested();return QStringLiteral("Камера сброшена.");}
    if(contains(text,QStringLiteral("полный экран|весь экран"))) {emit showRequested();emit fullscreenRequested();return QStringLiteral("Разворачиваю 3D-вьювер.");}
    if(contains(text,QStringLiteral("^(вращай|rotate|останови|stop|включи|выключи)"))) {
        const bool enabled=!contains(text,QStringLiteral("^(останови|stop|выключи)"));
        if(text.contains(QStringLiteral("пульс"))) setPulsing(enabled); else setRotating(enabled);
        emit showRequested();return enabled?QStringLiteral("Анимация включена."):QStringLiteral("Анимация остановлена.");
    }
    if(contains(text,QStringLiteral("^(измени|покрась|увеличь|уменьши)")) ||
       (contains(text,QStringLiteral("^сделай модел")) && (!colorIn(text).isEmpty() || !materialIn(text).isEmpty()))) {
        if(m_selected<0) return QStringLiteral("Сначала создайте 3D-модель.");
        if(!colorIn(text).isEmpty()) setParameter("color",colorIn(text));
        if(!materialIn(text).isEmpty()) setParameter("material",materialIn(text));
        if(text.startsWith(QStringLiteral("увеличь")) || text.startsWith(QStringLiteral("уменьши"))) {
            const double factor=text.startsWith(QStringLiteral("увеличь"))?1.25:0.8;
            for(const auto* key:{"width","height","depth"}) setParameter(key,m_items[m_selected].toMap().value(key,1).toDouble()*factor);
        }
        emit showRequested();return m_status;
    }
    const std::pair<const char*,const char*> kinds[]={{"\\bкуб\\w*\\b|\\bcube\\b","cube"},{"\\bдом(?:а|ик)?\\b|\\bhouse\\b","house"},
        {"\\bкружк\\w*\\b|\\bmug\\b","mug"},{"\\bсфер\\w*\\b|\\bsphere\\b","sphere"},{"\\bцилиндр\\w*\\b|\\bcylinder\\b","cylinder"},
        {"\\bтор(?:ус)?\\b|\\btorus\\b","torus"},{"график|поверхност|surface","surface"}};
    QString kind;
    for(const auto& [pattern,name]:kinds) if(contains(text,QString::fromUtf8(pattern))) {kind=name;break;}
    if(kind.isEmpty()) {emit showRequested();return QStringLiteral("Офлайн доступны куб, сфера, цилиндр, тор, дом, кружка и поверхность z=f(x,y).");}
    QVariantMap spec{{"kind",kind},{"color",colorIn(text).isEmpty()?"#b4c9df":colorIn(text)},
        {"material",materialIn(text).isEmpty()?"plastic":materialIn(text)},{"texture",text.contains(QStringLiteral("шахмат"))?"checker":"none"},
        {"resolution",48},{"width",1.0},{"height",1.0},{"depth",1.0}};
    for(const auto& [label,key]:std::initializer_list<std::pair<QString,QString>>{{QStringLiteral("ширин"),"width"},{QStringLiteral("высот"),"height"},{QStringLiteral("глубин"),"depth"}}) {
        const auto match=QRegularExpression(label+QStringLiteral("\\w*\\s*[:=]?\\s*(\\d+(?:[.,]\\d+)?)"),QRegularExpression::UseUnicodePropertiesOption).match(text);
        if(match.hasMatch()) spec[key]=std::clamp(QString(match.captured(1)).replace(',','.').toDouble(),0.1,5.0);
    }
    if(kind=="surface") {
        const auto match=QRegularExpression(QStringLiteral("(?:z|зет|зэд)\\s*(?:=|равно)\\s*(.+)$")).match(text);
        if(!match.hasMatch()) return QStringLiteral("Укажите формулу, например: покажи 3D-график z = sin(x) * cos(y).");
        QString formula=match.captured(1).trimmed();if(formula.endsWith('.')) formula.chop(1);
        formula.replace(QStringLiteral("умножить на"),"*").replace(QStringLiteral("плюс"),"+").replace(QStringLiteral("минус"),"-");
        formula.replace(QStringLiteral("синус икс"),"sin(x)").replace(QStringLiteral("косинус игрек"),"cos(y)");
        try {Expression validate(formula);Q_UNUSED(validate);} catch(const std::exception&) {return QStringLiteral("Формула не распознана. Используйте x, y, числа, + − * / ^ и sin, cos, tan, sqrt, abs, exp, log.");}
        spec["expression"]=formula;spec["resolution"]=96;
    }
    const bool append=text.startsWith(QStringLiteral("добавь")) || text.startsWith("add");
    auto candidate=append?m_items:QVariantList{};candidate.append(spec);
    if(!budget(candidate)) return QStringLiteral("Сцена ограничена 8 моделями и 1 миллионом треугольников.");
    m_drawingMode=false;emit drawingChanged();
    m_items=candidate;m_selected=static_cast<int>(m_items.size())-1;m_rotating=true;m_pulsing=text.contains(QStringLiteral("пульс"));
    m_status=QStringLiteral("Сцена: %1. ЛКМ — вращение, Ctrl + ЛКМ — перемещение, колесо — масштаб.").arg(kind);
    emit changed();emit showRequested();emit cameraReset();
    return QStringLiteral("Показываю 3D-модель.");
}
bool SceneController::submit(const QString& request) {
    const auto answer=handle(request);
    if(!answer) {m_status=QStringLiteral("Пример: покажи дом или покажи график z = sin(x)*cos(y).");emit changed();return false;}
    if(!answer->startsWith(QStringLiteral("Показываю"))) {m_status=*answer;emit changed();}
    return true;
}

void SceneController::requestExport(const QString& format) {
    if(m_exporting) return;
    m_exporting=true;
    m_status=QStringLiteral("Сохраняю модель…"); emit changed();
    const auto snapshot=m_items;
    const auto directory=exportDirectory;
    auto* watcher=new QFutureWatcher<QStringList>(this);
    connect(watcher,&QFutureWatcher<QStringList>::finished,this,[this,watcher] {
        const auto result=watcher->result(); m_exporting=false;
        m_lastExport=result[0]; m_status=result[1]; emit changed(); watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([snapshot,directory,format] {
        SceneController worker; worker.m_items=snapshot; worker.exportDirectory=directory;
        const auto path=worker.exportScene(format);
        return QStringList{path,worker.status()};
    }));
}

QString SceneController::exportScene(const QString& format) {
    if(m_items.isEmpty() || (format!="obj" && format!="gltf")) {m_status=QStringLiteral("Нет модели для экспорта.");emit changed();return {};}
    const QString directory=exportDirectory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/JARVIS/3D":exportDirectory;
    if(!QDir().mkpath(directory)) {m_status=QStringLiteral("Не удалось создать папку экспорта.");emit changed();return {};}
    const QString filename=directory+"/scene-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+"."+format;
    QByteArray output, buffer; QTextStream obj(&output); QJsonArray views,accessors,meshes,nodes,materials;
    quint32 offset=1;
    auto addAccessor=[&](const QByteArray& bytes,int count,int component,const QString& type,const QJsonArray& minimum=QJsonArray{},const QJsonArray& maximum=QJsonArray{}) {
        while(buffer.size()%4) buffer.append('\0');
        const int view=static_cast<int>(views.size());views.append(QJsonObject{{"buffer",0},{"byteOffset",static_cast<double>(buffer.size())},{"byteLength",static_cast<double>(bytes.size())}});buffer.append(bytes);
        QJsonObject a{{"bufferView",view},{"componentType",component},{"count",count},{"type",type}};
        if(!minimum.isEmpty()) {a["min"]=minimum;a["max"]=maximum;}
        const int index=static_cast<int>(accessors.size());accessors.append(a);return index;
    };
    for(qsizetype item=0;item<m_items.size();++item) {
        const auto spec=m_items[item].toMap();const auto mesh=generateMesh(spec);
        if(!mesh.error.isEmpty()) {m_status=QStringLiteral("Экспорт: ")+mesh.error;emit changed();return {};}
        const QColor color(spec.value("color").toString()); const float shift=(static_cast<float>(item)-(static_cast<float>(m_items.size())-1)/2)*3.5F;
        std::vector<float> positions,normals,colors,uvs;
        obj<<"o model_"<<item<<"\n";
        for(const auto& v:mesh.vertices) {
            positions.insert(positions.end(),{v.x+shift,v.y,v.z});normals.insert(normals.end(),{v.nx,v.ny,v.nz});uvs.insert(uvs.end(),{v.u,v.v});
            colors.insert(colors.end(),{v.r*color.redF(),v.g*color.greenF(),v.b*color.blueF(),1});
            obj<<"v "<<v.x+shift<<' '<<v.y<<' '<<v.z<<' '<<v.r*color.redF()<<' '<<v.g*color.greenF()<<' '<<v.b*color.blueF()<<"\n";
        }
        for(const auto& v:mesh.vertices) obj<<"vn "<<v.nx<<' '<<v.ny<<' '<<v.nz<<"\n";
        for(std::size_t i=0;i<mesh.indices.size();i+=3) {obj<<"f";for(int j=0;j<3;++j){const auto index=offset+mesh.indices[i+j];obj<<' '<<index<<"//"<<index;}obj<<"\n";}
        offset+=static_cast<quint32>(mesh.vertices.size());
        if(format=="gltf") {
            auto bytes=[](const auto& vector){return QByteArray(reinterpret_cast<const char*>(vector.data()),static_cast<qsizetype>(vector.size()*sizeof(vector[0])));};
            const int count=static_cast<int>(mesh.vertices.size());
            const int pos=addAccessor(bytes(positions),count,5126,"VEC3",{mesh.minimum.x()+shift,mesh.minimum.y(),mesh.minimum.z()},{mesh.maximum.x()+shift,mesh.maximum.y(),mesh.maximum.z()});
            const int normal=addAccessor(bytes(normals),count,5126,"VEC3"),tint=addAccessor(bytes(colors),count,5126,"VEC4"),uv=addAccessor(bytes(uvs),count,5126,"VEC2");
            const int index=addAccessor(bytes(mesh.indices),static_cast<int>(mesh.indices.size()),5125,"SCALAR");
            const QString material=spec.value("material").toString();
            const QJsonObject pbr{{"metallicFactor",material=="metal"?0.85:0.0},{"roughnessFactor",material=="plastic"?0.48:0.18},{"baseColorFactor",QJsonArray{1,1,1,material=="glass"?0.35:1.0}}};
            materials.append(QJsonObject{{"pbrMetallicRoughness",pbr},{"alphaMode",material=="glass"?"BLEND":"OPAQUE"},{"doubleSided",true}});
            const QJsonObject attributes{{"POSITION",pos},{"NORMAL",normal},{"COLOR_0",tint},{"TEXCOORD_0",uv}};
            const QJsonObject primitive{{"attributes",attributes},{"indices",index},{"material",static_cast<int>(item)}};
            meshes.append(QJsonObject{{"primitives",QJsonArray{primitive}}});
            nodes.append(QJsonObject{{"mesh",static_cast<int>(item)}});
        }
    }
    obj.flush();
    if(format=="gltf") {
        QJsonArray rootNodes;for(int i=0;i<nodes.size();++i) rootNodes.append(i);
        output=QJsonDocument(QJsonObject{{"asset",QJsonObject{{"version","2.0"},{"generator","JARVIS offline 3D"}}},{"scene",0},{"scenes",QJsonArray{QJsonObject{{"nodes",rootNodes}}}},
            {"nodes",nodes},{"meshes",meshes},{"materials",materials},{"accessors",accessors},{"bufferViews",views},
            {"buffers",QJsonArray{QJsonObject{{"byteLength",static_cast<double>(buffer.size())},{"uri","data:application/octet-stream;base64,"+QString::fromLatin1(buffer.toBase64())}}}}}).toJson();
    }
    QSaveFile file(filename);
    if(!file.open(QIODevice::WriteOnly) || file.write(output)!=output.size() || !file.commit()) {m_status=QStringLiteral("Не удалось сохранить модель.");emit changed();return {};}
    m_lastExport=filename;m_status=QStringLiteral("Экспорт сохранён: ")+filename;emit changed();return filename;
}
}
