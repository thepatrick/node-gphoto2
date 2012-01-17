#include "binding.h"
#include "camera.h"
#include <node_buffer.h>
#include <sstream>
#include <gphoto2/gphoto2-widget.h>
using namespace v8;
using namespace node;


Persistent<FunctionTemplate> GPCamera::constructor_template;

GPCamera::GPCamera(Handle<External> js_gphoto, std::string model, std::string port) : ObjectWrap(), model_(model), port_(port), camera_(NULL), config_(NULL){
  HandleScope scope;
  GPhoto2 *gphoto = static_cast<GPhoto2*>(js_gphoto->Value());
  this->gphoto = Persistent<External>::New(js_gphoto);
  this->gphoto_ = gphoto;
}
GPCamera::~GPCamera(){
  printf("Camera destructor\n");
  this->gphoto_->closeCamera(this);
  this->gphoto.Dispose();
  this->close();
}

void
GPCamera::Initialize(Handle<Object> target) {
    HandleScope scope;
    Local<FunctionTemplate> t = FunctionTemplate::New(New);
    
    // Constructor
    constructor_template = Persistent<FunctionTemplate>::New(t);
    
    
    constructor_template->InstanceTemplate()->SetInternalFieldCount(1);
    constructor_template->SetClassName(String::NewSymbol("Camera"));

    ADD_PROTOTYPE_METHOD(camera, getConfig, GetConfig);
    ADD_PROTOTYPE_METHOD(camera, getConfigValue, GetConfigValue);
    ADD_PROTOTYPE_METHOD(camera, setConfigValue, SetConfigValue);
    ADD_PROTOTYPE_METHOD(camera, takePicture, TakePicture);
    ADD_PROTOTYPE_METHOD(camera, getPreview, GetPreview);
    target->Set(String::NewSymbol("Camera"), constructor_template->GetFunction());
}

Handle<Value>
GPCamera::TakePicture(const Arguments& args) {
  HandleScope scope;
  printf("TakePicture %d\n", __LINE__);
  GPCamera *camera = ObjectWrap::Unwrap<GPCamera>(args.This());
  REQ_FUN_ARG(0, cb);
  take_picture_request *picture_req = new take_picture_request();
  picture_req->cb = Persistent<Function>::New(cb);
  picture_req->camera = camera->getCamera();
  picture_req->context = camera->gphoto_->getContext();
  eio_custom(EIO_TakePicture, EIO_PRI_DEFAULT, EIO_TakePictureCb, picture_req);
  ev_ref(EV_DEFAULT_UC);
  return Undefined();
}
void
GPCamera::EIO_TakePicture(eio_req *req){
  take_picture_request *picture_req = (take_picture_request *)req->data;
  printf("Taking picture\n");
  capture_to_memory(picture_req->camera, picture_req->context, &picture_req->data, static_cast<long unsigned int *>(&picture_req->length));
}
int
GPCamera::EIO_TakePictureCb(eio_req *req){
  HandleScope scope;
  ev_unref(EV_DEFAULT_UC);
  take_picture_request *picture_req = (take_picture_request *)req->data;
  
  node::Buffer* buffer = node::Buffer::New((char*)picture_req->data, picture_req->length);
  
  Handle<Value> argv[1];
  argv[0] = buffer->handle_;
  
  picture_req->cb->Call(Context::GetCurrent()->Global(), 1, argv);
  
  picture_req->cb.Dispose();
  delete picture_req;
  return 0;
}

// Return available configuration widgets as a list in the form
//  /main/status/model
//  /main/status/serialnumber
//  etc.
Handle<Value>
GPCamera::GetConfig(const Arguments& args) {
  HandleScope scope;
  REQ_FUN_ARG(0, cb)
  GPCamera *camera = ObjectWrap::Unwrap<GPCamera>(args.This());
  camera->Ref();
  get_config_request *config_req = new get_config_request();
  config_req->cameraObject = camera;
  config_req->camera = camera->getCamera();
  config_req->context = camera->gphoto_->getContext();
  config_req->cb = Persistent<Function>::New(cb);
  eio_custom(EIO_GetConfig, EIO_PRI_DEFAULT, EIO_GetConfigCb, config_req);
  ev_ref(EV_DEFAULT_UC);
  return Undefined();  
}

int GPCamera::enumConfig(get_config_request* req, CameraWidget *root, std::string prefix){
  int ret,n,i;
  std::ostringstream newprefix;
  char* label, *name, *uselabel;
	CameraWidgetType	type;
  gp_widget_get_label (root,(const char**)&label);
  ret = gp_widget_get_name (root, (const char**)&name);
	gp_widget_get_type (root, &type);
	if (std::string((const char*)name).length())
		uselabel = name;
	else
		uselabel = label;
	n = gp_widget_count_children(root);
	newprefix << prefix << "/" << uselabel;
	if ((type != GP_WIDGET_WINDOW) && (type != GP_WIDGET_SECTION)){
    req->settings.push_back(newprefix.str());
	}
	for (i=0; i<n; i++) {
		CameraWidget *child;
	
		ret = gp_widget_get_child(root, i, &child);
		if (ret != GP_OK)
			continue;
		enumConfig(req, child, newprefix.str());
	}
  return GP_OK;  
}

void GPCamera::EIO_GetConfig(eio_req *req){
  get_config_request *config_req = (get_config_request*)req->data;
  CameraWidget *config;
  int ret;
  ret = gp_camera_get_config(config_req->camera, &config, config_req->context);
  if(ret<GP_OK){config_req->ret=ret;return;}
  ret = enumConfig(config_req, config, "");
  gp_widget_free(config);
  config_req->ret = ret;
}
int GPCamera::EIO_GetConfigCb(eio_req *req){
  HandleScope scope;
  ev_unref(EV_DEFAULT_UC);
  get_config_request *config_req = (get_config_request*)req->data;
  
  Handle<Value> argv[2];
  if(config_req->ret == GP_OK){
    Local<Array> result = Array::New(config_req->settings.size());
    for(int i = 0; i < config_req->settings.size(); ++i){
      result->Set(Number::New(i),String::New(config_req->settings.at(i).c_str()));
    }
    argv[0] = Undefined();
    argv[1] = result;
  }
  else{
    argv[0] = Number::New(config_req->ret);
    argv[1] = Undefined();
  }

  config_req->cb->Call(Context::GetCurrent()->Global(), 2, argv);
  config_req->cb.Dispose();  
  config_req->cameraObject->Unref();
  delete config_req;
  return 0;
}
Handle<Value>
GPCamera::GetConfigValue(const Arguments& args) {
  HandleScope scope;
  GPCamera *camera = ObjectWrap::Unwrap<GPCamera>(args.This());
  camera->Ref();
  get_config_request *config_req = new get_config_request();
  config_req->cameraObject = camera;
  config_req->camera       = camera->getCamera();
  REQ_STR_ARG(0, key);
  REQ_FUN_ARG(1, cb);
  
  return Undefined();
}

void
GPCamera::EIO_GetConfigValue(eio_req *req){
  get_config_request *config_req = (get_config_request *)req->data;
  
}
int
GPCamera::EIO_GetConfigValueCb(eio_req *req){
  HandleScope scope;
  ev_unref(EV_DEFAULT_UC);

  get_config_request *config_req = (get_config_request *)req->data;
  config_req->cameraObject->Unref();
  delete config_req;  
  return 0;
}

Handle<Value>
GPCamera::SetConfigValue(const Arguments& args) {
  HandleScope scope;
  GPCamera *camera = ObjectWrap::Unwrap<GPCamera>(args.This());
  camera->Ref();
  set_config_request *config_req = new set_config_request();
  
  config_req->camera = camera;
  REQ_STR_ARG(0, key);
  REQ_STR_ARG(0, value);
  REQ_FUN_ARG(1, cb);
  
  return Undefined();
}
void
GPCamera::EIO_SetConfigValue(eio_req *req){
  get_config_request *config_req = (get_config_request *)req->data;
  
}
int
GPCamera::EIO_SetConfigValueCb(eio_req *req){
  HandleScope scope;
  ev_unref(EV_DEFAULT_UC);

  get_config_request *config_req = (get_config_request *)req->data;
  config_req->cameraObject->Unref();
  return 0;
}

Handle<Value>
GPCamera::New(const Arguments& args) {
  HandleScope scope;
  
  REQ_EXT_ARG(0, js_gphoto);
  REQ_STR_ARG(1, model_);
  REQ_STR_ARG(2, port_);

  GPCamera *camera = new GPCamera(js_gphoto, (std::string)*model_, (std::string)*port_);
  camera->Wrap(args.This());
  Local<Object> This = args.This();
  This->Set(String::New("model"),String::New(camera->model_.c_str()));
  This->Set(String::New("port"),String::New(camera->port_.c_str()));
  return args.This();
}  

Camera* GPCamera::getCamera(){
  GPhoto2 *gp = this->gphoto_;
    //printf("getCamera %s gphoto=%p\n", this->isOpen() ? "open" : "closed", gp);
  if(!this->isOpen()){
    printf("Opening camera %s with portList=%p abilitiesList=%p\n", this->model_.c_str(),this->gphoto_->getPortInfoList(), this->gphoto_->getAbilitiesList());
    this->gphoto_->openCamera(this);
  } 
  return this->camera_;
};


Handle<Value>
GPCamera::GetPreview(const Arguments& args) {
  HandleScope scope;
  GPCamera *camera = ObjectWrap::Unwrap<GPCamera>(args.This());
  camera->Ref();
  REQ_FUN_ARG(0, cb);
  take_picture_request *preview_req = new take_picture_request();

  preview_req->cb = Persistent<Function>::New(cb);
  preview_req->camera = camera->getCamera();
  preview_req->context = camera->gphoto_->getContext();
  preview_req->cameraObject = camera;
  
  eio_custom(EIO_CapturePreview, EIO_PRI_DEFAULT, EIO_CapturePreviewCb, preview_req);
  ev_ref(EV_DEFAULT_UC);
  return scope.Close(Undefined());
    
}
void GPCamera::EIO_CapturePreview(eio_req *req){
  int ret;

  take_picture_request *preview_req = (take_picture_request*) req->data;
  
  RETURN_ON_ERROR(preview_req, gp_file_new, (&preview_req->file), {});
  RETURN_ON_ERROR(preview_req, gp_camera_capture_preview, (preview_req->camera, preview_req->file, preview_req->context), {gp_file_free(preview_req->file);});
  unsigned long int length;
  RETURN_ON_ERROR(preview_req, gp_file_get_data_and_size, (preview_req->file, &preview_req->data, &length), {gp_file_free(preview_req->file);});
  preview_req->length = (size_t)length;
  
}

int GPCamera::EIO_CapturePreviewCb(eio_req *req){
  HandleScope scope;
  ev_unref(EV_DEFAULT_UC);
  int ret;
  CameraFile *file;
  take_picture_request *preview_req = (take_picture_request*) req->data;
  Handle<Value> argv[2];
  int argc = 1;
  if(preview_req->ret < GP_OK){
    argv[0] = Integer::New(preview_req->ret);
  }
  else if(preview_req->data) {
    argc = 2;
    argv[0] = Undefined();
    node::Buffer* slowBuffer = node::Buffer::New(preview_req->length);
    memcpy(Buffer::Data(slowBuffer), preview_req->data, preview_req->length);
    Local<Object> globalObj = Context::GetCurrent()->Global();
    Local<Function> bufferConstructor = Local<Function>::Cast(globalObj->Get(v8::String::New("Buffer")));
    Handle<Value> constructorArgs[3] = { slowBuffer->handle_, v8::Integer::New(preview_req->length), v8::Integer::New(0) };
    Local<Object> actualBuffer = bufferConstructor->NewInstance(3, constructorArgs);
    argv[1] =actualBuffer;
  }
  
  preview_req->cb->Call(Context::GetCurrent()->Global(), argc, argv);
  preview_req->cb.Dispose();
  if(preview_req->ret == GP_OK)  gp_file_free(preview_req->file);
  preview_req->cameraObject->Unref();
  delete preview_req;  
  return 0;  
}

bool
GPCamera::close(){
  // this->gphoto_->Unref();
  if(this->camera_)
    return gp_camera_exit(this->camera_, this->gphoto_->getContext()) < GP_OK ? false : true;
  else
    return true;    
}
